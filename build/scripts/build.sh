#!/usr/bin/env bash
# build.sh - Internal build orchestrator (runs inside Docker container)
#
# This script runs INSIDE the Docker toolchain container. It orchestrates
# building all SecureOS components. Called by: scripts/build.sh (host-side)
# via docker run.
#
# Usage: build.sh [kernel|disk|all]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="${1:-all}"

case "$TARGET" in
  kernel)
    "$ROOT_DIR/build/scripts/build_kernel_entry.sh"
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    ;;
  disk)
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    ;;
  all)
    echo "[build] Building all components..."
    "$ROOT_DIR/build/scripts/build_bearssl.sh"
    "$ROOT_DIR/build/scripts/build_kernel_entry.sh"
    "$ROOT_DIR/build/scripts/build_user_lib.sh" envlib
    "$ROOT_DIR/build/scripts/build_user_app.sh" filedemo
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    echo "[build] All components built successfully"
    ;;
  -h|--help)
    echo "Usage: build.sh [kernel|disk|all]"
    echo "  kernel  - Compile kernel and create ISO"
    echo "  disk    - Build disk image with OS binaries"
    echo "  all     - Build everything (default)"
    exit 0
    ;;
  *)
    echo "Unknown target: $TARGET"
    echo "Usage: build.sh [kernel|disk|all]"
    exit 1
    ;;
esac