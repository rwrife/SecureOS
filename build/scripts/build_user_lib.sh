#!/usr/bin/env bash
# build_user_lib.sh - Compile a user-space library into SOF format
#
# This script runs INSIDE the Docker toolchain container. It compiles a
# user library from user/libs/<name>/ into artifacts/lib/<name>.lib (SOF).
# Called by: build/scripts/build.sh
#
# Usage: build_user_lib.sh [lib_name]  (default: envlib)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/lib"
LIB_NAME="${1:-envlib}"

build_user_lib_inner() {
  LIB_DIR="user/libs/$LIB_NAME"
  local src_path
  local object_path
  local object_files=""
  local user_cflags="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -I user/include"
  local user_ldflags="-m elf_x86_64 -nostdlib -e main"
  test -f "$LIB_DIR/main.c"
  mkdir -p artifacts/lib

  # Skip netlib if BearSSL objects are not available (due to toolchain issues)
  if [ "$LIB_NAME" = "netlib" ] && ! compgen -G "artifacts/bearssl/*.o" >/dev/null; then
    echo "WARNING: Skipping netlib build (BearSSL not available)"
    return 0
  fi

  # Add BearSSL include path for netlib (TLS/HTTPS support)
  EXTRA_INCLUDES=""
  EXTRA_LINK_OBJECTS=""
  if [ "$LIB_NAME" = "netlib" ] && [ -d "vendor/bearssl/BearSSL/inc" ]; then
    EXTRA_INCLUDES="-nostdlibinc -I vendor/bearssl/include -I vendor/bearssl/BearSSL/inc"
    if [ -d "artifacts/bearssl" ]; then
      for bobj in artifacts/bearssl/*.o; do
        [ -f "$bobj" ] && EXTRA_LINK_OBJECTS="$EXTRA_LINK_OBJECTS $bobj"
      done
    fi
  fi

  for src_path in "$LIB_DIR"/*.c; do
    case "$src_path" in
      *_kernel.c)
        continue
        ;;
    esac
    object_path="artifacts/lib/${LIB_NAME}_$(basename "$src_path" .c).o"
    clang $user_cflags $EXTRA_INCLUDES \
      -c "$src_path" -o "$object_path"
    object_files="$object_files $object_path"
  done

  clang $user_cflags -c user/runtime/secureos_api_stubs.c -o artifacts/lib/secureos_api_stubs.o
  ld.lld $user_ldflags \
    -o "artifacts/lib/$LIB_NAME.elf" $object_files artifacts/lib/secureos_api_stubs.o $EXTRA_LINK_OBJECTS

  # Build sof_wrap if not already built
  if [ ! -x "tools/sof_wrap/sof_wrap" ]; then
    make -C tools/sof_wrap
  fi

  # Wrap ELF in SOF container (signed if keys available)
  SIGN_ARGS=""
  if [ -f "artifacts/keys/intermediate.seed" ] && [ -f "artifacts/keys/intermediate.cert" ]; then
    SIGN_ARGS="--sign-key artifacts/keys/intermediate.seed --sign-cert artifacts/keys/intermediate.cert"
  fi

  ./tools/sof_wrap/sof_wrap \
    --type lib --name "$LIB_NAME" --author "SecureOS" --version "1.0.0" \
    --date "$(date -u +%Y-%m-%d)" \
    $SIGN_ARGS \
    "artifacts/lib/$LIB_NAME.elf" "artifacts/lib/$LIB_NAME.lib"
  echo "Built artifacts/lib/$LIB_NAME.lib"
}

mkdir -p "$OUT_DIR"
build_user_lib_inner
echo "PASS: user lib build ($LIB_NAME)"
