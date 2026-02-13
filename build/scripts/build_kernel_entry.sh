#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/kernel"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

mkdir -p "$OUT_DIR"

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc '
  set -euo pipefail
  nasm -f elf32 kernel/arch/x86/boot/entry.asm -o artifacts/kernel/entry.o
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -c kernel/core/kmain.c -o artifacts/kernel/kmain.o
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -c kernel/arch/x86/serial.c -o artifacts/kernel/serial.o
  clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -c kernel/arch/x86/vga.c -o artifacts/kernel/vga.o
  ld.lld -m elf_i386 -T kernel/arch/x86/boot/linker.ld \
    -Map=artifacts/kernel/kernel.map \
    -o artifacts/kernel/kernel.elf \
    artifacts/kernel/entry.o artifacts/kernel/kmain.o artifacts/kernel/serial.o artifacts/kernel/vga.o
  if command -v llvm-objdump >/dev/null 2>&1; then
    llvm-objdump -h artifacts/kernel/kernel.elf > artifacts/kernel/kernel.sections.txt
  else
    objdump -h artifacts/kernel/kernel.elf > artifacts/kernel/kernel.sections.txt
  fi
  echo "Built artifacts/kernel/kernel.elf"
'

echo "PASS: kernel entry/linker build"
