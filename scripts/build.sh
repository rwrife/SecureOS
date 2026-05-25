#!/usr/bin/env bash
# scripts/build.sh - Host-side build entry point (macOS/Linux)
#
# Invokes the Docker toolchain container to compile SecureOS.
# All compilation happens inside the container — this script just
# orchestrates the docker run.
#
# Usage: scripts/build.sh [kernel|disk|all|app <name>|force]  (default: all)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="$ROOT_DIR/build/docker/Dockerfile.toolchain"
TARGET="${1:-all}"
EXTRA_ARGS="${2:-}"

# Ensure Docker is available
if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: Docker is required. Run ./scripts/setup-macos.sh or ./scripts/setup-linux.sh"
  exit 1
fi

if ! docker info >/dev/null 2>&1; then
  echo "ERROR: Docker daemon is not running. Start Docker Desktop or the docker service."
  exit 1
fi

# Build toolchain image if it doesn't exist
if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  echo "Building toolchain image: $IMAGE_TAG"
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

# Run the build inside the container
echo "Building SecureOS (target: $TARGET $EXTRA_ARGS)..."
docker run --rm \
  -v "$ROOT_DIR":/workspace \
  -w /workspace \
  "$IMAGE_TAG" \
  bash -lc "set -euo pipefail; ./build/scripts/build.sh $TARGET $EXTRA_ARGS"

# Fix permissions on artifacts (container may create as root)
if [[ -d "$ROOT_DIR/artifacts" ]]; then
  chmod -R a+rw "$ROOT_DIR/artifacts" 2>/dev/null || true
fi

echo ""
echo "✓ Build complete. Artifacts:"
[[ -f "$ROOT_DIR/artifacts/kernel/secureos.iso" ]] && echo "  - artifacts/kernel/secureos.iso" || true
[[ -f "$ROOT_DIR/artifacts/kernel/kernel.elf" ]]   && echo "  - artifacts/kernel/kernel.elf"   || true
[[ -f "$ROOT_DIR/artifacts/disk/secureos-disk.img" ]] && echo "  - artifacts/disk/secureos-disk.img" || true
