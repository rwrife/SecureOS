#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ISO_ROOT="$ROOT_DIR/artifacts/iso"
KERNEL_ELF="$ROOT_DIR/artifacts/kernel/kernel.elf"
ISO_PATH="$ROOT_DIR/artifacts/kernel/secureos.iso"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

build_kernel_image_inner() {
  test -f artifacts/kernel/kernel.elf
  grub-file --is-x86-multiboot artifacts/kernel/kernel.elf
  rm -rf artifacts/iso
  mkdir -p artifacts/iso/boot/grub
  cp build/grub/grub.cfg artifacts/iso/boot/grub/grub.cfg
  cp artifacts/kernel/kernel.elf artifacts/iso/boot/kernel.elf
  grub-mkrescue -o artifacts/kernel/secureos.iso artifacts/iso >/dev/null 2>&1
  echo "Built artifacts/kernel/secureos.iso"
}

"$ROOT_DIR/build/scripts/build_kernel_entry.sh"

mkdir -p "$ROOT_DIR/artifacts/kernel"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
  fi

  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc 'set -euo pipefail; ./build/scripts/build_kernel_image.sh'
else
  build_kernel_image_inner
fi

echo "PASS: kernel ISO build"
