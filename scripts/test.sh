#!/usr/bin/env bash
# scripts/test.sh - Host-side test runner (macOS/Linux)
#
# Runs the SecureOS test suite inside the Docker toolchain container.
#
# Usage: scripts/test.sh [test_name|--all]  (default: --all)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
DOCKERFILE="$ROOT_DIR/build/docker/Dockerfile.toolchain"
TEST_NAME="${1:---all}"

# Ensure Docker is available
if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "ERROR: Docker is required and must be running."
  exit 1
fi

# Build toolchain image if it doesn't exist
if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  echo "Building toolchain image: $IMAGE_TAG"
  docker build -f "$DOCKERFILE" -t "$IMAGE_TAG" "$ROOT_DIR"
fi

# Map --all to test.sh's default behavior
if [[ "$TEST_NAME" == "--all" ]]; then
  TEST_ARG=""
else
  TEST_ARG="$TEST_NAME"
fi

echo "Running tests${TEST_ARG:+ ($TEST_ARG)}..."
docker run --rm \
  -v "$ROOT_DIR":/workspace \
  -w /workspace \
  "$IMAGE_TAG" \
  bash -lc "set -euo pipefail; ./build/scripts/test.sh $TEST_ARG"
