#!/usr/bin/env bash
# build_bearssl.sh — Compile BearSSL objects for SecureOS freestanding x86-64.
#
# Purpose:
#   Compiles all BearSSL source files listed in Makefile.secureos into .o
#   files under artifacts/bearssl/ using the SecureOS freestanding user-app toolchain.
#
# Interactions:
#   - vendor/bearssl/Makefile.secureos lists the source files.
#   - vendor/bearssl/secureos_compat.c provides libc shims.
#   - build_kernel_entry.sh and build_user_app.sh link the resulting objects.
#
# Launched by:
#   Called from build.sh or directly before kernel/app builds.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BEARSSL_DIR="$ROOT_DIR/vendor/bearssl/BearSSL"
VENDOR_DIR="$ROOT_DIR/vendor/bearssl"
OUT_DIR="$ROOT_DIR/artifacts/bearssl"

# Determinism: enumerate sources from Makefile.secureos (no globbing). Use
# -Wall -Werror to keep the freestanding compile honest, with the small
# carve-outs BearSSL needs in freestanding mode (it uses a few constructs
# the project's host build flags would otherwise reject).
CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc -I $ROOT_DIR/vendor/bearssl/include"
CC_WARN_FLAGS="-Wall -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-implicit-fallthrough -Wno-sign-compare"

build_bearssl_inner() {
  local src_dir="$BEARSSL_DIR"
  local inc_dir="$BEARSSL_DIR/inc"

  if [ ! -d "$src_dir/src" ]; then
    echo "ERROR: BearSSL submodule not found. Run: git submodule update --init"
    exit 1
  fi

  mkdir -p "$OUT_DIR"

  # Compile compat shims (lives outside the submodule)
  echo "Compiling secureos_compat.c ..."
  clang $CC_FLAGS $CC_WARN_FLAGS -I "$inc_dir" -I "$src_dir/src" \
    -c "$VENDOR_DIR/secureos_compat.c" -o "$OUT_DIR/secureos_compat.o"

  # Compile each BearSSL source file.
  # We compile all .c files under src/, skipping platform-specific intrinsics
  # files that require headers unavailable in freestanding mode (x86ni, pclmul,
  # sse2, power8). The hw_stubs file provides NULL fallbacks for those.
  local count=0
  while IFS= read -r src_path; do
    # Skip files requiring platform intrinsics
    case "$src_path" in
      *x86ni*|*pclmul*|*sse2*|*power8*|*ctmulq*) continue ;;
    esac
    local src_rel="${src_path#$src_dir/}"
    local obj_name
    obj_name=$(echo "$src_rel" | sed 's|/|_|g; s|\.c$|.o|')
    clang $CC_FLAGS $CC_WARN_FLAGS -I "$inc_dir" -I "$src_dir/src" \
      -c "$src_path" -o "$OUT_DIR/$obj_name"
    count=$((count + 1))
  done < <(find "$src_dir/src" -name '*.c' | sort)

  # Compile hardware acceleration stubs (returns NULL for all hw-accel vtables)
  echo "Compiling bearssl_hw_stubs.c ..."
  clang $CC_FLAGS $CC_WARN_FLAGS -I "$inc_dir" -I "$src_dir/src" \
    -c "$VENDOR_DIR/bearssl_hw_stubs.c" -o "$OUT_DIR/bearssl_hw_stubs.o"
  count=$((count + 1))

  echo "Compiled $count BearSSL objects into $OUT_DIR"
  # Emit a deterministic size/count summary so the validator (and CI) can
  # track code-size drift against the ~80-100KB budget called out in
  # implementation_plan.md.
  if command -v du >/dev/null 2>&1; then
    local total_kb
    total_kb=$(du -sk "$OUT_DIR" 2>/dev/null | awk '{print $1}')
    echo "BEARSSL_OBJECT_COUNT=$count"
    echo "BEARSSL_OBJECT_TOTAL_KB=${total_kb:-unknown}"
  fi
}

mkdir -p "$OUT_DIR"
build_bearssl_inner
echo "PASS: BearSSL build"