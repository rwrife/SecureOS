#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="${SECUREOS_TOOLCHAIN_DOCKERFILE:-$ROOT_DIR/build/docker/Dockerfile.toolchain}"

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

echo "Launching interactive SecureOS console..."
echo "Type commands at the secureos> prompt. Use 'exit pass' to stop QEMU cleanly."

docker run --rm -it -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" bash -lc './build/scripts/run_qemu.sh --test kernel_prompt'
RC=$?
if [[ "$RC" -eq 33 ]]; then
  echo "QEMU_PASS:kernel_prompt"
  exit 0
fi
exit "$RC"
