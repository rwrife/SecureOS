#!/usr/bin/env bash
# build_kernel_image.sh - Create bootable ISO from kernel.elf using GRUB
#
# This script runs INSIDE the Docker toolchain container. It takes the
# compiled kernel.elf and wraps it into a bootable ISO using grub-mkrescue.
# Called by: scripts/build.sh (via docker run)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Ensure kernel is compiled first
"$ROOT_DIR/build/scripts/build_kernel_entry.sh"

mkdir -p "$ROOT_DIR/artifacts/kernel"

test -f artifacts/kernel/kernel.elf || { echo "ERROR: kernel.elf not found"; exit 1; }
grub-file --is-x86-multiboot artifacts/kernel/kernel.elf

rm -rf artifacts/iso
mkdir -p artifacts/iso/boot/grub
cp build/grub/grub.cfg artifacts/iso/boot/grub/grub.cfg
cp artifacts/kernel/kernel.elf artifacts/iso/boot/kernel.elf
grub-mkrescue -o artifacts/kernel/secureos.iso artifacts/iso >/dev/null 2>&1

echo "Built artifacts/kernel/secureos.iso"