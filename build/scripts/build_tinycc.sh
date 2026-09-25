#!/usr/bin/env bash
# build_tinycc.sh — Build the freestanding TinyCC compiler archives.
#
# Purpose:
#   Compiles the nine pinned libtcc translation units plus the SecureOS
#   no-JIT runtime shim into artifacts/user/libs/libtcc.a. It also builds
#   the freestanding libclib.a dependency (including setjmp_x86.S) and the
#   libtcc1.a runtime-helper archive used by programs emitted by TinyCC.
#
# Called by:
#   build/scripts/build.sh (targets `tinycc`, `all`, and `force`) and
#   build/scripts/test_tinycc_freestanding_link.sh.
#
# This script runs inside the pinned toolchain container. Upstream files in
# vendor/tinycc/tinycc remain unmodified; configuration is supplied by the
# SecureOS wrapper headers under vendor/tinycc/.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor/tinycc"
SUBMODULE_DIR="$VENDOR_DIR/tinycc"
OBJ_DIR="$ROOT_DIR/artifacts/tinycc/core-obj"
LIB_DIR="$ROOT_DIR/artifacts/user/libs"

if ! command -v clang >/dev/null 2>&1 || ! command -v ar >/dev/null 2>&1; then
  echo "BUILD_TINYCC:FAIL:toolchain_not_found" >&2
  exit 1
fi
if [ ! -d "$SUBMODULE_DIR" ] || [ -z "$(ls -A "$SUBMODULE_DIR" 2>/dev/null)" ]; then
  echo "BUILD_TINYCC:FAIL:tinycc_submodule_not_initialized" >&2
  exit 1
fi

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

CORE_COUNT="$(printf '%s\n' "$CORE_SRCS" | grep -c '\.c$' || true)"
if [ "$CORE_COUNT" -ne 9 ]; then
  echo "BUILD_TINYCC:FAIL:core_source_count:$CORE_COUNT" >&2
  exit 1
fi

TCC_VERSION="$(grep -E '^Commit:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' "$VENDOR_DIR/VERSION" \
  | awk '{print $2}' | cut -c1-10 || echo unknown)"
CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc"
WARN_FLAGS="-Wall -Wno-unused -Wno-typedef-redefinition -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-missing-braces -Wno-incompatible-function-pointer-types"
DEFINES="-DONE_SOURCE=0 -DCONFIG_TCC_SEMLOCK=0 -DCONFIG_TCC_STATIC"
INCLUDES="-I $VENDOR_DIR/include -I $ROOT_DIR/user/include -I $ROOT_DIR/user/libs/clib/include/clib -I $SUBMODULE_DIR"

rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR" "$LIB_DIR"
objects=()
while IFS= read -r src; do
  [ -z "$src" ] && continue
  obj="$OBJ_DIR/${src%.c}.o"
  # shellcheck disable=SC2086
  clang $CC_FLAGS $WARN_FLAGS $DEFINES -DTCC_VERSION="\"$TCC_VERSION\"" \
    -include "$VENDOR_DIR/config-secureos.h" \
    -include "$VENDOR_DIR/include/tcc-compat.h" \
    $INCLUDES -c "$SUBMODULE_DIR/$src" -o "$obj"
  objects+=("$obj")
done <<EOF
$CORE_SRCS
EOF

# Upstream tcc_delete() references tcc_run_free on native x86-64 even when
# the SecureOS port excludes hosted tccrun.c. The wrapper is deliberately
# no-op because this build never creates JIT runtime allocations.
RUNTIME_OBJ="$OBJ_DIR/secureos_runtime.o"
clang $CC_FLAGS -Wall -Wextra -Werror \
  -c "$VENDOR_DIR/secureos_runtime.c" -o "$RUNTIME_OBJ"
objects+=("$RUNTIME_OBJ")

# Normalize member mtimes and use deterministic archive mode.
find "$OBJ_DIR" -name '*.o' -exec touch -h -d '@0' {} +
LIBTCC_A="$LIB_DIR/libtcc.a"
rm -f "$LIBTCC_A"
ar Drcs "$LIBTCC_A" "${objects[@]}"

# Build libclib.a with all C and assembly TUs, then build the emitted-code
# runtime archive from its pinned manifest.
(
  cd "$ROOT_DIR"
  "$ROOT_DIR/build/scripts/build_user_lib.sh" clib
)
"$ROOT_DIR/build/scripts/build_tinycc_libtcc1.sh"

printf 'TINYCC_CORE_OBJECT_COUNT=%s\n' "$CORE_COUNT"
printf 'TINYCC_ARCHIVE=%s\n' "$LIBTCC_A"
printf 'TINYCC_CLIB_ARCHIVE=%s\n' "$LIB_DIR/libclib.a"
printf 'TINYCC_RUNTIME_ARCHIVE=%s\n' "$LIB_DIR/libtcc1.a"
printf 'BUILD_TINYCC:PASS\n'
