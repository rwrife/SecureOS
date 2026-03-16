#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/user"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
APP_NAME="${1:-filedemo}"

build_user_app_inner() {
  APP_DIR="user/apps/$APP_NAME"
  test -f "$APP_DIR/main.c"
  mkdir -p artifacts/user
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 \
    -I user/include \
    -c "$APP_DIR/main.c" -o "artifacts/user/$APP_NAME.o"
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 \
    -I user/include \
    -c user/runtime/secureos_api_stubs.c -o artifacts/user/secureos_api_stubs.o
  ld.lld -m elf_i386 -nostdlib -e main \
    -o "artifacts/user/$APP_NAME.elf" "artifacts/user/$APP_NAME.o" artifacts/user/secureos_api_stubs.o

  # Build sof_wrap if not already built
  if [ ! -f "tools/sof_wrap/sof_wrap" ]; then
    make -C tools/sof_wrap
  fi

  # Wrap ELF in SOF container
  ./tools/sof_wrap/sof_wrap \
    --type bin --name "$APP_NAME" --author "SecureOS" --version "1.0.0" \
    --date "$(date -u +%Y-%m-%d)" \
    "artifacts/user/$APP_NAME.elf" "artifacts/user/$APP_NAME.bin"
  echo "Built artifacts/user/$APP_NAME.bin"
}

mkdir -p "$OUT_DIR"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
  fi

  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc "set -euo pipefail; ./build/scripts/build_user_app.sh '$APP_NAME'"
else
  build_user_app_inner
fi

echo "PASS: user app build ($APP_NAME)"
