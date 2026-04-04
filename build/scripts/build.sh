#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="${SECUREOS_TOOLCHAIN_DOCKERFILE:-$ROOT_DIR/build/docker/Dockerfile.toolchain}"
TARGET="${1:-image}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [kernel|modules|image|run|test-boot|user-app|user-lib|disk|console|graphics|build-all]

Builds SecureOS targets using the pinned toolchain container.
Environment overrides:
  SECUREOS_TOOLCHAIN_IMAGE      Container image tag (default: $IMAGE_TAG)
  SECUREOS_TOOLCHAIN_DOCKERFILE Dockerfile path for bootstrap build
EOF
}

if [[ "${TARGET}" == "-h" || "${TARGET}" == "--help" ]]; then
  usage
  exit 0
fi

command -v docker >/dev/null 2>&1 || { echo "docker is required"; exit 1; }

toolchain_image_valid() {
  docker run --rm "$IMAGE_TAG" bash -lc 'command -v grub-mkrescue >/dev/null 2>&1; test -d /usr/lib/grub/i386-pc' >/dev/null 2>&1
}

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1 || ! toolchain_image_valid; then
  echo "Toolchain image not found: $IMAGE_TAG"
  echo "Building it from: $DOCKERFILE"
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

case "$TARGET" in
  kernel)
    "$ROOT_DIR/build/scripts/build_kernel_entry.sh"
    ;;
  modules|image)
    if [[ "$TARGET" == "image" ]]; then
      "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    else
      echo "[build] target=$TARGET"
      docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" \
        bash -lc "echo TODO: implement $TARGET target build graph"
    fi
    ;;
  run)
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_console
    ;;
  test-boot)
    "$ROOT_DIR/build/scripts/test.sh" hello_boot
    ;;
  user-app)
    "$ROOT_DIR/build/scripts/build_user_app.sh" filedemo
    ;;
  user-lib)
    "$ROOT_DIR/build/scripts/build_user_lib.sh" envlib
    ;;
  disk)
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    ;;
  console)
    "$ROOT_DIR/build/scripts/boot_console.sh"
    ;;
  graphics)
    "$ROOT_DIR/build/scripts/boot_graphics.sh"
    ;;
  build-all)
    echo "[build] target=build-all - building all components for testing"
    "$ROOT_DIR/build/scripts/build_bearssl.sh"
    "$ROOT_DIR/build/scripts/build_kernel_entry.sh"
    "$ROOT_DIR/build/scripts/build_user_lib.sh" envlib
    "$ROOT_DIR/build/scripts/build_user_app.sh" filedemo
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    echo "[build] Completed all components for testing"
    ;;
  *)
    echo "Unknown target: $TARGET"
    usage
    exit 1
    ;;
esac
