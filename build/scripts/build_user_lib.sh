#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/lib"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
LIB_NAME="${1:-envlib}"

build_user_lib_inner() {
  LIB_DIR="user/libs/$LIB_NAME"
  test -f "$LIB_DIR/main.c"
  mkdir -p artifacts/lib
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 \
    -I user/include \
    -c "$LIB_DIR/main.c" -o "artifacts/lib/$LIB_NAME.o"
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 \
    -I user/include \
    -c user/runtime/secureos_api_stubs.c -o artifacts/lib/secureos_api_stubs.o
  ld.lld -m elf_i386 -nostdlib -e main \
    -o "artifacts/lib/$LIB_NAME.elf" "artifacts/lib/$LIB_NAME.o" artifacts/lib/secureos_api_stubs.o

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
