#!/usr/bin/env bash
# start.sh - SecureOS one-command launcher
#
# Clone the repo, run this script, and SecureOS boots in QEMU.
# Handles dependency setup, build, and boot in one step.
#
# Usage:
#   ./start.sh [--setup-only] [--build-only] [--graphics] [--skip-setup] [--clean]
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Parse flags ---
SETUP_ONLY=0
BUILD_ONLY=0
GRAPHICS=0
SKIP_SETUP=0
CLEAN=0

for arg in "$@"; do
  case "$arg" in
    --setup-only) SETUP_ONLY=1 ;;
    --build-only) BUILD_ONLY=1 ;;
    --graphics)   GRAPHICS=1 ;;
    --skip-setup) SKIP_SETUP=1 ;;
    --clean)      CLEAN=1 ;;
    -h|--help)
      cat <<EOF
Usage: ./start.sh [OPTIONS]

Options:
  --setup-only   Install dependencies only (don't build or boot)
  --build-only   Build the OS but don't boot it
  --graphics     Boot with VGA display window instead of serial console
  --skip-setup   Skip dependency checks (assumes Docker + QEMU installed)
  --clean        Remove artifacts before building
  -h, --help     Show this help message
EOF
      exit 0
      ;;
    *)
      echo "Unknown option: $arg"
      echo "Run ./start.sh --help for usage."
      exit 1
      ;;
  esac
done

# --- Banner ---
echo ""
echo "╔══════════════════════════════════════╗"
echo "║          SecureOS Launcher           ║"
echo "╚══════════════════════════════════════╝"
echo ""

# --- Step 1: Dependency checks ---
if [[ "$SKIP_SETUP" -eq 0 ]]; then
  echo "[1/3] Checking dependencies..."

  NEED_SETUP=0
  if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo "  ✗ Docker not found or not running"
    NEED_SETUP=1
  else
    echo "  ✓ Docker"
  fi

  if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "  ✗ QEMU not found"
    NEED_SETUP=1
  else
    echo "  ✓ QEMU"
  fi

  if [[ "$NEED_SETUP" -eq 1 ]]; then
    echo ""
    read -rp "Missing dependencies. Run setup script? [Y/n] " response
    response="${response:-Y}"
    if [[ "$response" =~ ^[Yy] ]]; then
      case "$(uname -s)" in
        Darwin)
          bash "$ROOT_DIR/scripts/setup-macos.sh"
          ;;
        Linux)
          bash "$ROOT_DIR/scripts/setup-linux.sh"
          ;;
        *)
          echo "ERROR: Unsupported platform. Run scripts/setup-*.sh manually."
          exit 1
          ;;
      esac
    else
      echo "Setup skipped. Install Docker and QEMU manually, then re-run."
      exit 1
    fi

    # Re-verify after setup
    if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
      echo "ERROR: Docker still not available after setup."
      echo "You may need to start Docker Desktop or log out/in for group changes."
      exit 1
    fi
    if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
      echo "ERROR: QEMU still not available after setup."
      exit 1
    fi
  fi
  echo ""
else
  echo "[1/3] Skipping dependency checks (--skip-setup)"
  echo ""
fi

if [[ "$SETUP_ONLY" -eq 1 ]]; then
  echo "Setup complete. Dependencies are installed."
  exit 0
fi

# --- Step 2: Build ---
echo "[2/3] Building SecureOS..."

if [[ "$CLEAN" -eq 1 ]]; then
  echo "  Cleaning artifacts..."
  rm -rf "$ROOT_DIR/artifacts"
fi

bash "$ROOT_DIR/scripts/build.sh" all
echo ""

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Build complete. Artifacts in artifacts/"
  exit 0
fi

# --- Step 3: Boot ---
echo "[3/3] Booting SecureOS in QEMU..."
echo ""

if [[ "$GRAPHICS" -eq 1 ]]; then
  bash "$ROOT_DIR/scripts/boot.sh" graphics
else
  bash "$ROOT_DIR/scripts/boot.sh" console
fi
