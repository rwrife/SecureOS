#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EXPERIMENT_DIR="$ROOT_DIR/experiments/bootloader"
BOOT_ASM="$EXPERIMENT_DIR/boot_fail.asm"
BOOT_BIN="$EXPERIMENT_DIR/boot_fail.bin"

if [[ ! -f "$BOOT_ASM" ]]; then
  echo "Missing failing fixture source: $BOOT_ASM"
  exit 1
fi

command -v nasm >/dev/null 2>&1 || { echo "nasm is required"; exit 1; }

nasm -f bin "$BOOT_ASM" -o "$BOOT_BIN"

if [[ ! -f "$BOOT_BIN" ]]; then
  echo "Failed to build failing boot sector fixture"
  exit 1
fi

BOOT_SIZE=$(wc -c < "$BOOT_BIN" | tr -d '[:space:]')
if [[ "$BOOT_SIZE" -ne 512 ]]; then
  echo "Failing fixture size is not 512 bytes (got $BOOT_SIZE)"
  exit 1
fi

echo "PASS: built failing boot sector fixture"
