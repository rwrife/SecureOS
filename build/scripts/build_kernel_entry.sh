#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/kernel"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

build_kernel_entry_inner() {
  # Compile entry.asm as elf64 (contains both 32-bit and 64-bit code for the
  # Multiboot → long-mode transition)
  nasm -f elf64 kernel/arch/x86/boot/entry.asm -o artifacts/kernel/entry.o

  # All C kernel code compiled for x86-64 freestanding
  CFLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -mno-mmx -mno-sse -mno-sse2"
  clang $CFLAGS -c kernel/core/kmain.c -o artifacts/kernel/kmain.o
  clang $CFLAGS -c kernel/core/console.c -o artifacts/kernel/console.o
  clang $CFLAGS -c kernel/core/session_manager.c -o artifacts/kernel/session_manager.o
  clang $CFLAGS -c kernel/sched/scheduler.c -o artifacts/kernel/scheduler.o
  clang $CFLAGS -c kernel/drivers/disk/ata_pio.c -o artifacts/kernel/ata_pio.o
  clang $CFLAGS -c kernel/arch/x86/debug_exit.c -o artifacts/kernel/debug_exit.o
  clang $CFLAGS -c kernel/arch/x86/serial.c -o artifacts/kernel/serial.o
  clang $CFLAGS -c kernel/arch/x86/vga.c -o artifacts/kernel/vga.o
  clang $CFLAGS -c kernel/cap/cap_table.c -o artifacts/kernel/cap_table.o
  clang $CFLAGS -c kernel/event/event_bus.c -o artifacts/kernel/event_bus.o
  clang $CFLAGS -c kernel/hal/network_hal.c -o artifacts/kernel/network_hal.o
  clang $CFLAGS -c kernel/hal/serial_hal.c -o artifacts/kernel/serial_hal.o
  clang $CFLAGS -c kernel/hal/storage_hal.c -o artifacts/kernel/storage_hal.o
  clang $CFLAGS -c kernel/hal/video_hal.c -o artifacts/kernel/video_hal.o
  clang $CFLAGS -c kernel/drivers/disk/ramdisk.c -o artifacts/kernel/ramdisk.o
  clang $CFLAGS -c kernel/drivers/network/virtio_net.c -o artifacts/kernel/virtio_net.o
  clang $CFLAGS -c kernel/drivers/serial/pc_com.c -o artifacts/kernel/pc_com.o
  clang $CFLAGS -c kernel/drivers/video/vga_text.c -o artifacts/kernel/vga_text.o
  clang $CFLAGS -c kernel/drivers/video/framebuffer_text_stub.c -o artifacts/kernel/framebuffer_text_stub.o
  clang $CFLAGS -c kernel/drivers/video/gpio_text_stub.c -o artifacts/kernel/gpio_text_stub.o
  clang $CFLAGS -c kernel/crypto/sha512.c -o artifacts/kernel/sha512.o
  clang $CFLAGS -c kernel/crypto/ed25519.c -o artifacts/kernel/ed25519.o
  clang $CFLAGS -c kernel/crypto/cert.c -o artifacts/kernel/cert.o
  clang $CFLAGS -c kernel/format/sof.c -o artifacts/kernel/sof.o
  clang $CFLAGS -c kernel/fs/fs_service.c -o artifacts/kernel/fs_service.o
  clang $CFLAGS -c kernel/user/native_net_service.c -o artifacts/kernel/native_net_service.o
  clang $CFLAGS -c kernel/user/process.c -o artifacts/kernel/process.o

  ld.lld -m elf_x86_64 -T kernel/arch/x86/boot/linker.ld \
    -Map=artifacts/kernel/kernel.map \
    -o artifacts/kernel/kernel.elf \
    artifacts/kernel/entry.o artifacts/kernel/kmain.o artifacts/kernel/console.o artifacts/kernel/session_manager.o artifacts/kernel/scheduler.o artifacts/kernel/ata_pio.o artifacts/kernel/debug_exit.o artifacts/kernel/serial.o artifacts/kernel/vga.o artifacts/kernel/cap_table.o artifacts/kernel/event_bus.o artifacts/kernel/network_hal.o artifacts/kernel/serial_hal.o artifacts/kernel/storage_hal.o artifacts/kernel/video_hal.o artifacts/kernel/ramdisk.o artifacts/kernel/virtio_net.o artifacts/kernel/pc_com.o artifacts/kernel/vga_text.o artifacts/kernel/framebuffer_text_stub.o artifacts/kernel/gpio_text_stub.o artifacts/kernel/sha512.o artifacts/kernel/ed25519.o artifacts/kernel/cert.o artifacts/kernel/sof.o artifacts/kernel/fs_service.o artifacts/kernel/native_net_service.o artifacts/kernel/process.o
  if command -v llvm-objdump >/dev/null 2>&1; then
    llvm-objdump -h artifacts/kernel/kernel.elf > artifacts/kernel/kernel.sections.txt
  else
    objdump -h artifacts/kernel/kernel.elf > artifacts/kernel/kernel.sections.txt
  fi
  echo "Built artifacts/kernel/kernel.elf"
}

toolchain_image_valid() {
  docker run --rm "$IMAGE_TAG" bash -lc 'command -v grub-mkrescue >/dev/null 2>&1; test -d /usr/lib/grub/i386-pc' >/dev/null 2>&1
}

mkdir -p "$OUT_DIR"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1 || ! toolchain_image_valid; then
    docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
  fi

  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc 'set -euo pipefail; ./build/scripts/build_kernel_entry.sh'
else
  build_kernel_entry_inner
fi

echo "PASS: kernel entry/linker build"
