#!/usr/bin/env bash
# test_tinycc_vendor_gate.sh — Deterministic drift gate for the vendored
# TinyCC slice (issue #408 Phase 1 scaffold sub-slice; mirrors the BearSSL
# `bearssl_compile` gate from #117).
#
# Purpose:
#   The actual freestanding TinyCC port (#408) is a multi-session effort.
#   In the meantime, the vendor-side surface that the port will consume
#   (`vendor/tinycc/Makefile.secureos`, `vendor/tinycc/VERSION`, the
#   pinned submodule commit, and the list of in-scope source files) must
#   not silently drift — otherwise the port's "land standalone, fold in
#   later" path (same pattern as #404 / `clib_malloc` and #117 / BearSSL)
#   has no canary catching a quiet vendor surface change.
#
# What we assert:
#   1. `vendor/tinycc/Makefile.secureos` exists and enumerates the .c
#      source list explicitly (no glob/wildcard).
#   2. `TCC_CORE_SRCS` and `TCC_TARGET_SRCS` are non-empty and union to
#      the minimum file count the port's libtcc build needs (libtcc
#      core + x86_64 backend + i386 shared assembler = 9 files today).
#   3. `vendor/tinycc/VERSION` exists and pins a 40-hex `Commit:` SHA
#      that matches `.gitmodules` / the actual submodule HEAD when the
#      submodule is initialized (SKIPs the live-SHA cross-check
#      gracefully when the submodule has not been cloned, same way
#      `bearssl_compile`'s freestanding-objects step SKIPs when
#      `artifacts/bearssl/` is empty).
#   4. The deliberately-excluded surfaces called out in
#      `vendor/tinycc/Makefile.secureos`'s porting notes (`tccrun.c`,
#      `tcctools.c`, `tcc.c`, `tccpe.c`, `tccmacho.c`, `tcccoff.c`, and
#      every non-x86_64 backend: arm-*, arm64-*, riscv64-*, c67-*) are
#      NOT in `TCC_ALL_SRCS`. This is the actual drift gate: prevents
#      an accidental scope expansion into JIT, the CLI driver, non-ELF
#      object formats, or other-arch backends slipping into the in-OS
#      compiler surface without an audit.
#   5. When the submodule IS initialized, every file listed in the
#      Makefile actually exists under `vendor/tinycc/tinycc/`.
#
# Emits TEST:PASS:<sub> / TEST:SKIP:<sub>:<reason> / TEST:FAIL:<sub>:<reason>
# markers so the validator JSON report (#110) can key on individual checks,
# same shape as `bearssl_compile`.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor/tinycc"
SUBMODULE_DIR="$VENDOR_DIR/tinycc"
MAKEFILE="$VENDOR_DIR/Makefile.secureos"
VERSION_FILE="$VENDOR_DIR/VERSION"

echo "TEST:START:tinycc_vendor_gate"

fail() {
  echo "TEST:FAIL:tinycc_vendor_gate:$1"
  exit 1
}

# 1. Makefile.secureos exists, no globbing.
if [ ! -f "$MAKEFILE" ]; then
  fail "missing_makefile_secureos"
fi

if grep -E '\*\.c|\$\(wildcard|\bwildcard\b' "$MAKEFILE" >/dev/null 2>&1; then
  fail "makefile_uses_globbing"
fi
echo "TEST:PASS:tinycc_vendor_gate_makefile_present"

# 2. Source list is non-empty and meets the documented minimum.
#    Parse only continuation-line .c entries; ignore commentary that
#    happens to mention filenames (e.g. the EXCLUDED list in the porting
#    notes uses prose, not Makefile variables).
SRC_LIST="$(awk '
  /^[[:space:]]*#/ { next }
  /=[[:space:]]*\\?[[:space:]]*$/ { in_list=1; next }
  in_list {
    if ($0 ~ /^[[:space:]]*$/) { in_list=0; next }
    if ($0 !~ /\\$/) in_list=0
    line=$0
    sub(/\\[[:space:]]*$/, "", line)
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
    if (line ~ /\.c$/) print line
  }
' "$MAKEFILE" | sort -u)"

SRC_COUNT=$(printf '%s\n' "$SRC_LIST" | grep -c '\.c$' || true)
# libtcc core (6) + x86_64 backend (2) + shared i386 assembler (1) = 9.
# Any future deliberate addition bumps this floor *in the same PR* that
# changes Makefile.secureos.
if [ "$SRC_COUNT" -lt 9 ]; then
  fail "makefile_src_list_too_small:$SRC_COUNT"
fi
echo "TEST:PASS:tinycc_vendor_gate_makefile_enumerated:$SRC_COUNT"

# 3. VERSION pin file: present, well-formed 40-hex Commit: line.
if [ ! -f "$VERSION_FILE" ]; then
  fail "missing_version_pin"
fi
PIN_SHA=$(grep -E '^Commit:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' "$VERSION_FILE" \
  | awk '{print $2}' | head -1 || true)
if [ -z "$PIN_SHA" ]; then
  fail "version_pin_missing_or_malformed_commit"
fi
echo "TEST:PASS:tinycc_vendor_gate_version_pinned:$PIN_SHA"

# 3b. Pin SHA matches `.gitmodules`-recorded submodule commit when the
#     submodule has been initialized. SKIP when not initialized (matches
#     bearssl_compile's "no artifacts yet" SKIP discipline).
if [ -d "$SUBMODULE_DIR/.git" ] || [ -f "$SUBMODULE_DIR/.git" ]; then
  LIVE_SHA=$(git -C "$ROOT_DIR" submodule status -- vendor/tinycc/tinycc 2>/dev/null \
    | awk '{print $1}' | sed 's/^[+-]//' | head -1 || true)
  if [ -z "$LIVE_SHA" ]; then
    echo "TEST:SKIP:tinycc_vendor_gate_pin_matches_submodule:submodule_status_unavailable"
  elif [ "$LIVE_SHA" != "$PIN_SHA" ]; then
    fail "pin_sha_does_not_match_submodule:${PIN_SHA}_vs_${LIVE_SHA}"
  else
    echo "TEST:PASS:tinycc_vendor_gate_pin_matches_submodule:$LIVE_SHA"
  fi
else
  echo "TEST:SKIP:tinycc_vendor_gate_pin_matches_submodule:submodule_not_initialized"
fi

# 4. Deliberately-excluded surfaces must NOT appear in the source list.
#    This is the actual drift gate: a future PR that quietly pulls in
#    `tccrun.c` (JIT), `tcc.c` (CLI main), `tccpe.c` / `tccmacho.c` /
#    `tcccoff.c` (non-ELF formats), `tcctools.c` (ar/makedef), or any
#    non-x86_64 backend would silently expand the in-OS compiler's
#    surface and break the freestanding port's audit invariants.
EXCLUDED=(
  tccrun.c
  tcctools.c
  tcc.c
  tccpe.c
  tccmacho.c
  tcccoff.c
  arm-gen.c
  arm-link.c
  arm-asm.c
  arm64-gen.c
  arm64-link.c
  arm64-asm.c
  riscv64-gen.c
  riscv64-link.c
  riscv64-asm.c
  c67-gen.c
  c67-link.c
)
for excl in "${EXCLUDED[@]}"; do
  if printf '%s\n' "$SRC_LIST" | grep -Fxq "$excl"; then
    fail "excluded_source_in_makefile:$excl"
  fi
done
echo "TEST:PASS:tinycc_vendor_gate_excluded_sources_absent:${#EXCLUDED[@]}"

# 5. When the submodule is initialized, every listed source must exist.
if [ -d "$SUBMODULE_DIR" ] && [ -n "$(ls -A "$SUBMODULE_DIR" 2>/dev/null || true)" ]; then
  MISSING=""
  while IFS= read -r src; do
    [ -z "$src" ] && continue
    if [ ! -f "$SUBMODULE_DIR/$src" ]; then
      MISSING="$MISSING $src"
    fi
  done <<EOF
$SRC_LIST
EOF
  if [ -n "$MISSING" ]; then
    fail "listed_source_missing_from_submodule:$(echo $MISSING | tr ' ' ',')"
  fi
  echo "TEST:PASS:tinycc_vendor_gate_listed_sources_present:$SRC_COUNT"
else
  echo "TEST:SKIP:tinycc_vendor_gate_listed_sources_present:submodule_not_initialized"
fi

echo "TEST:PASS:tinycc_vendor_gate"
