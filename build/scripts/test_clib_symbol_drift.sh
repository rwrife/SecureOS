#!/usr/bin/env bash
# build/scripts/test_clib_symbol_drift.sh
#
# M7-TOOLCHAIN-004 / issue #449: drift gate for the public symbol
# surface of `libclib.a` (user/libs/clib/).
#
# Three sources of truth must agree:
#   1. `tests/data/clib_symbols.expected`    -- canonical pin file (one
#      symbol per line, sorted, terminated by trailing newline).
#   2. `docs/abi/clib-symbols.md`            -- human-readable doc, with
#      a fenced block between the `<!-- clib-symbols:begin -->` and
#      `<!-- clib-symbols:end -->` markers that mirrors (1).
#   3. The actual host-side build of `libclib.a` from
#      `user/libs/clib/src/*.c`, surveyed via `nm -g --defined-only`.
#
# Sub-markers (consumed by build/scripts/test.sh + validate_bundle.sh):
#   TEST:PASS:clib_symbol_drift:pin_sorted
#   TEST:PASS:clib_symbol_drift:doc_matches_pin
#   TEST:PASS:clib_symbol_drift:libclib_matches_pin
#   TEST:PASS:clib_symbol_drift:no_undocumented_export
#   TEST:PASS:clib_symbol_drift
#
# Any divergence between the three emits a deterministic
# TEST:FAIL:clib_symbol_drift:<reason> marker on stderr and exits 1.
#
# No env deps beyond a C compiler, ar, nm, sort, diff, grep, awk.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests/clib_symbol_drift"
SRC_DIR="$ROOT_DIR/user/libs/clib/src"
INC_DIR="$ROOT_DIR/user/libs/clib/include"
PIN_FILE="$ROOT_DIR/tests/data/clib_symbols.expected"
DOC_FILE="$ROOT_DIR/docs/abi/clib-symbols.md"

fail() {
  printf 'TEST:FAIL:clib_symbol_drift:%s\n' "$1" >&2
  exit 1
}

mkdir -p "$OUT_DIR"

# --- preconditions ---
[ -r "$PIN_FILE" ] || fail "pin_file_missing"
[ -r "$DOC_FILE" ] || fail "doc_file_missing"
[ -d "$SRC_DIR" ]  || fail "src_dir_missing"

# --- 1. pin_sorted ----------------------------------------------------
# Pin file must be sorted (C locale) and have no duplicates / no blanks.
if ! awk 'NF==0 {bad=1} END {exit bad}' "$PIN_FILE"; then
  fail "pin_has_blank_lines"
fi
PIN_SORTED="$OUT_DIR/pin_sorted.txt"
LC_ALL=C sort -u "$PIN_FILE" > "$PIN_SORTED"
if ! diff -u "$PIN_FILE" "$PIN_SORTED" > "$OUT_DIR/pin_sort.diff" 2>&1; then
  cat "$OUT_DIR/pin_sort.diff" >&2
  fail "pin_not_sorted_or_has_duplicates"
fi
echo "TEST:PASS:clib_symbol_drift:pin_sorted"

# --- 2. doc_matches_pin ----------------------------------------------
# Extract the canonical block between clib-symbols:begin / :end.
DOC_BLOCK="$OUT_DIR/doc_block.txt"
awk '
  /<!-- clib-symbols:begin -->/ {inblk=1; next}
  /<!-- clib-symbols:end -->/   {inblk=0}
  inblk {print}
' "$DOC_FILE" \
  | awk '/^```/ {fence=!fence; next} fence {print}' \
  > "$DOC_BLOCK"

if [ ! -s "$DOC_BLOCK" ]; then
  fail "doc_block_empty_or_missing_markers"
fi

if ! diff -u "$PIN_FILE" "$DOC_BLOCK" > "$OUT_DIR/doc_vs_pin.diff" 2>&1; then
  echo "doc canonical block disagrees with pin file:" >&2
  cat "$OUT_DIR/doc_vs_pin.diff" >&2
  fail "doc_disagrees_with_pin"
fi
echo "TEST:PASS:clib_symbol_drift:doc_matches_pin"

# --- 3. libclib_matches_pin ------------------------------------------
# Build a host-side libclib.a from user/libs/clib/src/*.c and compare
# its public defined symbols to the pin file. We compile with
# -fno-builtin (parity with the per-slice tests) so str*/mem* are not
# resolved to host builtins. Compiler default ABI is fine -- the symbol
# *names* are stable across hosts.
# Note: `user/include` is added so the `clib_os_brk` forwarder
# (slice 3 of M7-TOOLCHAIN-001, issue #421) can resolve
# `secureos_api.h` — the only kernel-adjacent header the
# freestanding clib library depends on. This mirrors what
# `build/scripts/build_user_lib.sh` does for the SDK build.
# `validate_sdk_no_kernel_includes` still pins that the include
# chain stays inside `user/include`.
USER_INC_DIR="$ROOT_DIR/user/include"
CFLAGS="-std=c11 -Wall -Wextra -Werror -fno-builtin -I${INC_DIR} -I${USER_INC_DIR}"

OBJS=()
for src in "$SRC_DIR"/*.c; do
  obj="$OUT_DIR/$(basename "$src" .c).o"
  cc $CFLAGS -c "$src" -o "$obj"
  OBJS+=("$obj")
done

ARCHIVE="$OUT_DIR/libclib.a"
rm -f "$ARCHIVE"
ar rcs "$ARCHIVE" "${OBJS[@]}"

# `nm -g --defined-only` over the archive gives one block per member,
# each line `<addr> <type> <name>`. We want only externally-defined
# code (T) and data (D, B, R) symbols, dropping local helpers (which
# nm -g already excludes).
LIB_SYMS="$OUT_DIR/libclib_symbols.txt"
nm -g --defined-only "$ARCHIVE" \
  | awk '$2 ~ /^[TDBR]$/ {print $3}' \
  | LC_ALL=C sort -u \
  > "$LIB_SYMS"

if ! diff -u "$PIN_FILE" "$LIB_SYMS" > "$OUT_DIR/libclib_vs_pin.diff" 2>&1; then
  echo "host-built libclib.a public symbols disagree with pin file:" >&2
  echo "  (- = expected by pin, + = found in libclib.a)" >&2
  cat "$OUT_DIR/libclib_vs_pin.diff" >&2
  fail "libclib_disagrees_with_pin"
fi
echo "TEST:PASS:clib_symbol_drift:libclib_matches_pin"

# --- 4. no_undocumented_export ---------------------------------------
# Belt-and-suspenders: every symbol that ended up in libclib.a must
# also appear in the doc's per-header tables (not just the canonical
# block). Catches the failure mode "someone added a row only to the
# canonical block and forgot to put it in a header section".
MISSING_FROM_TABLES="$OUT_DIR/missing_from_doc_tables.txt"
: > "$MISSING_FROM_TABLES"
# Strip the canonical block out of the doc first so a match against
# only the block doesn't count.
DOC_TABLES_ONLY="$OUT_DIR/doc_tables_only.txt"
awk '
  /<!-- clib-symbols:begin -->/ {skip=1}
  !skip {print}
  /<!-- clib-symbols:end -->/   {skip=0}
' "$DOC_FILE" > "$DOC_TABLES_ONLY"

while IFS= read -r sym; do
  # Look for the symbol fenced in backticks anywhere in the table text.
  if ! grep -qF "\`$sym\`" "$DOC_TABLES_ONLY"; then
    echo "$sym" >> "$MISSING_FROM_TABLES"
  fi
done < "$PIN_FILE"

if [ -s "$MISSING_FROM_TABLES" ]; then
  echo "symbols in pin but missing from docs/abi/clib-symbols.md header tables:" >&2
  cat "$MISSING_FROM_TABLES" >&2
  fail "symbol_missing_from_doc_table"
fi
echo "TEST:PASS:clib_symbol_drift:no_undocumented_export"

# --- roll-up ---------------------------------------------------------
echo "TEST:PASS:clib_symbol_drift"
