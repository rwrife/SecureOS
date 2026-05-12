#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/lib"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
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
    EXTRA_INCLUDES="-I vendor/bearssl/BearSSL/inc"
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
  if [ ! -f "tools/sof_wrap/sof_wrap" ]; then
    make -C tools/sof_wrap
  fi

  # Wrap ELF in SOF container
  ./tools/sof_wrap/sof_wrap \
    --type lib --name "$LIB_NAME" --author "SecureOS" --version "1.0.0" \
    --date "$(date -u +%Y-%m-%d)" \
    "artifacts/lib/$LIB_NAME.elf" "artifacts/lib/$LIB_NAME.lib"
  echo "Built artifacts/lib/$LIB_NAME.lib"
}

mkdir -p "$OUT_DIR"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
  fi

  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc "set -euo pipefail; ./build/scripts/build_user_lib.sh '$LIB_NAME'"
else
  build_user_lib_inner
fi

echo "PASS: user lib build ($LIB_NAME)"
