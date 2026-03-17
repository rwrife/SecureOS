#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="${SECUREOS_TOOLCHAIN_DOCKERFILE:-$ROOT_DIR/build/docker/Dockerfile.toolchain}"
QEMU_ARGS_FILE="$ROOT_DIR/build/qemu/x86_64-graphical.args"
ISO_PATH="$ROOT_DIR/artifacts/kernel/secureos.iso"
DISK_PATH="$ROOT_DIR/artifacts/disk/secureos-disk.img"

toolchain_image_valid() {
  docker run --rm "$IMAGE_TAG" bash -lc 'command -v grub-mkrescue >/dev/null 2>&1; test -d /usr/lib/grub/i386-pc' >/dev/null 2>&1
}

stop_secureos_instances() {
  mapfile -t IDS < <(docker ps --filter "ancestor=$IMAGE_TAG" --format "{{.ID}}")
  if [[ ${#IDS[@]} -gt 0 ]]; then
    echo "Stopping existing SecureOS toolchain containers..."
    docker stop "${IDS[@]}" >/dev/null 2>&1 || true
  fi

  if command -v pkill >/dev/null 2>&1; then
    pkill -f "qemu-system-x86_64.*secureos-disk.img" >/dev/null 2>&1 || true
    pkill -f "qemu-system-x86_64.*secureos.iso" >/dev/null 2>&1 || true
  fi
}

command -v docker >/dev/null 2>&1 || { echo "docker is required"; exit 1; }

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1 || ! toolchain_image_valid; then
  echo "Toolchain image not found or incomplete: $IMAGE_TAG"
  echo "Building it from: $DOCKERFILE"
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

stop_secureos_instances

"$ROOT_DIR/build/scripts/build_kernel_image.sh"
"$ROOT_DIR/build/scripts/build_disk_image.sh"

if [[ ! -f "$QEMU_ARGS_FILE" ]]; then
  echo "Missing QEMU args file: $QEMU_ARGS_FILE"
  exit 1
fi
if [[ ! -f "$ISO_PATH" ]]; then
  echo "Missing kernel ISO: $ISO_PATH"
  exit 1
fi
if [[ ! -f "$DISK_PATH" ]]; then
  echo "Missing disk image: $DISK_PATH"
  exit 1
fi

command -v qemu-system-x86_64 >/dev/null 2>&1 || {
  echo "qemu-system-x86_64 is required on host PATH for graphical mode."
  exit 1
}

mapfile -t RAW_ARGS < <(sed -e 's/\r$//' -e '/^\s*#/d' -e '/^\s*$/d' "$QEMU_ARGS_FILE")

echo "Launching SecureOS in QEMU graphical mode..."
echo "Use 'exit pass' in the SecureOS console to stop cleanly."
echo "Input note: type commands in this terminal (serial), not in the QEMU graphics window."

set +e
qemu-system-x86_64 \
  -cdrom "$ISO_PATH" \
  -boot d \
  -drive "format=raw,file=$DISK_PATH,if=ide,index=0,media=disk" \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  "${RAW_ARGS[@]}"
RC=$?
set -e

if [[ "$RC" -eq 33 ]]; then
  echo "QEMU_PASS:kernel_prompt"
  exit 0
fi
if [[ "$RC" -eq 35 ]]; then
  echo "QEMU_FAIL:kernel_prompt:debug_exit=fail"
  exit 1
fi
exit "$RC"
