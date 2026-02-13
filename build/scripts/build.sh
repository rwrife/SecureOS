#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="${SECUREOS_TOOLCHAIN_DOCKERFILE:-$ROOT_DIR/build/docker/Dockerfile.toolchain}"
TARGET="${1:-image}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [kernel|modules|image|run|test-boot]

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

if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  echo "Toolchain image not found: $IMAGE_TAG"
  echo "Building it from: $DOCKERFILE"
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

case "$TARGET" in
  kernel|modules|image)
    echo "[build] target=$TARGET"
    docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" \
      bash -lc "echo TODO: implement $TARGET target build graph"
    ;;
  run)
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot
    ;;
  test-boot)
    "$ROOT_DIR/build/scripts/test.sh" hello_boot
    ;;
  *)
    echo "Unknown target: $TARGET"
    usage
    exit 1
    ;;
esac
