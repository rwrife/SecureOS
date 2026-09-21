#!/usr/bin/env bash
# test_tinycc_freestanding_compile.sh — host gate for the TinyCC
# freestanding first-compile slice (issue #766 / #408 Phase 3).
#
# Purpose:
#   Turns vendor/tinycc/Makefile.secureos's scaffold into a MEASURED
#   build fact: compiles every pinned core TU (TCC_ALL_SRCS) with the
#   SecureOS freestanding userland flags + the vendored config/include
#   shims, and builds the libtcc1.a runtime-helper archive via
#   build/scripts/build_tinycc_libtcc1.sh. This proves the
#   config-secureos.h / libc-deps.json / libtcc1-srcs.json pin triangle
#   is actually consistent with the pinned submodule — the thing the
#   Phase 2 scaffolds asserted but never executed.
#
#   Scope honesty (issue #766 acceptance criteria): this gate proves
#   COMPILABILITY of the pinned source set against the SecureOS libc
#   surface. It does NOT claim an executable in-OS compiler: the link
#   step, loader integration, memory budget, and in-guest compile proof
#   remain open #766 work. fdopen/strerror/strtod/ldexpl are
#   declaration-only shims (vendor/tinycc/include/tcc-compat.h); a link
#   that reaches them fails loudly.
#
# Sub-markers:
#   tinycc_freestanding_compile:<tu>.o              per core TU compiled
#   tinycc_freestanding_compile:libtcc1_archive     libtcc1.a built
#   tinycc_freestanding_compile:libtcc1_members     member count == pin
#   tinycc_freestanding_compile                     rollup
#
# SKIP arms (toolchain-absent environments, mirrors bearssl_compile):
#   no clang on PATH / submodule uninitialized.
#
# Emits TEST:PASS / TEST:FAIL markers for the validate_bundle report.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor/tinycc"
SUBMODULE_DIR="$VENDOR_DIR/tinycc"
OUT_DIR="$ROOT_DIR/artifacts/tests/tinycc_freestanding_compile"
GATE="tinycc_freestanding_compile"

emit() { printf '%s\n' "$1"; }
pass() { emit "TEST:PASS:$1"; }
fail() { emit "TEST:FAIL:$GATE:$1"; ALL_OK=0; }

ALL_OK=1

# --- toolchain / submodule preconditions ------------------------------------
if ! command -v clang >/dev/null 2>&1 || ! command -v ar >/dev/null 2>&1; then
  emit "TEST:SKIP:$GATE:no_toolchain_on_path"
  emit "TEST:PASS:$GATE"
  exit 0
fi

if [ ! -d "$SUBMODULE_DIR" ] || [ -z "$(ls -A "$SUBMODULE_DIR" 2>/dev/null)" ]; then
  emit "TEST:SKIP:$GATE:tinycc_submodule_not_initialized"
  emit "TEST:PASS:$GATE"
  exit 0
fi

# --- core TU set: read TCC_CORE_SRCS + TCC_TARGET_SRCS from
# Makefile.secureos (no globbing; TCC_ALL_SRCS is just their union).
CORE_SRCS="$(awk '
  /^[[:space:]]*#/ { next }
  /^(TCC_CORE_SRCS|TCC_TARGET_SRCS)[[:space:]]*=/ { in_list=1; next }
  in_list {
    if ($0 ~ /^[[:space:]]*$/) { in_list=0; next }
    if ($0 !~ /\\$/) in_list=0
    line=$0
    sub(/\\[[:space:]]*$/, "", line)
    gsub(/^[[:space:]]+|[[:space:]]+$/, "", line)
    if (line ~ /\.c$/) print line
  }
' "$VENDOR_DIR/Makefile.secureos" | grep -v '^$' | sort -u)"

CORE_COUNT=$(printf '%s\n' "$CORE_SRCS" | grep -c '\.c$' || true)
if [ "$CORE_COUNT" -ne 9 ]; then
  fail "core_src_count:$CORE_COUNT"
fi

# --- flags (mirror build_user_app.sh freestanding posture) ------------------
mkdir -p "$OUT_DIR"
CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc"
WARN_FLAGS="-Wall -Wno-unused -Wno-typedef-redefinition -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-missing-braces -Wno-incompatible-function-pointer-types"
DEFINES="-DONE_SOURCE=0 -DCONFIG_TCC_SEMLOCK=0 -DCONFIG_TCC_STATIC"
INCLUDES="-I $VENDOR_DIR/include -I $ROOT_DIR/user/include -I $ROOT_DIR/user/libs/clib/include/clib -I $SUBMODULE_DIR"

TCC_VERSION="$(grep -E '^Commit:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' "$VENDOR_DIR/VERSION" \
  | awk '{print $2}' | cut -c1-10 || echo unknown)"

# --- compile each pinned core TU --------------------------------------------
while IFS= read -r src; do
  [ -z "$src" ] && continue
  base="${src%.c}"
  if clang $CC_FLAGS $WARN_FLAGS $DEFINES -DTCC_VERSION="\"$TCC_VERSION\"" \
      -include "$VENDOR_DIR/config-secureos.h" \
      -include "$VENDOR_DIR/include/tcc-compat.h" \
      $INCLUDES \
      -c "$SUBMODULE_DIR/$src" -o "$OUT_DIR/$base.o" 2>"$OUT_DIR/$base.log"; then
    pass "$GATE:$base.o"
  else
    fail "compile_error:$base" "$(head -c 400 "$OUT_DIR/$base.log" | tr '\n' ';')"
  fi
done <<EOF
$CORE_SRCS
EOF

# --- build the libtcc1.a runtime archive via the production script ----------
if "$ROOT_DIR/build/scripts/build_tinycc_libtcc1.sh" > "$OUT_DIR/libtcc1_build.log" 2>&1; then
  if [ -f "$ROOT_DIR/artifacts/user/libs/libtcc1.a" ]; then
    pass "$GATE:libtcc1_archive"
  else
    fail "libtcc1_archive_missing" "$(head -c 300 "$OUT_DIR/libtcc1_build.log" | tr '\n' ';')"
  fi
else
  fail "libtcc1_build_failed" "$(head -c 400 "$OUT_DIR/libtcc1_build.log" | tr '\n' ';')"
fi

# Member-count parity with the pinned manifest (drift tripwire).
if command -v python3 >/dev/null 2>&1 && [ -f "$ROOT_DIR/artifacts/user/libs/libtcc1.a" ]; then
  EXPECTED="$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["required"]))' \
    "$VENDOR_DIR/libtcc1-srcs.json" 2>/dev/null || echo 0)"
  ACTUAL="$(ar t "$ROOT_DIR/artifacts/user/libs/libtcc1.a" 2>/dev/null | wc -l | tr -d ' ')"
  if [ "$EXPECTED" = "$ACTUAL" ] && [ "$EXPECTED" != "0" ]; then
    pass "$GATE:libtcc1_members:$ACTUAL"
  else
    fail "libtcc1_members:expected=$EXPECTED:actual=$ACTUAL"
  fi
fi

if [ "$ALL_OK" -eq 1 ]; then
  pass "$GATE"
  exit 0
else
  emit "TEST:FAIL:$GATE"
  exit 1
fi
