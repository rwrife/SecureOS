#!/usr/bin/env bash
set -euo pipefail

# @file build_os_command.sh
# @brief Build isolated OS command script binaries and wrap them in SOF format.
#
# Purpose:
#   Converts individual OS command script source files into minimal ELF payloads,
#   then wraps each in the SecureOS File Format (SOF) container so process_run()
#   can load them dynamically from /os at runtime.
#
# Usage:
#   ./build_os_command.sh <command_name>
#   Example: ./build_os_command.sh ls
#
# Output:
#   artifacts/os/<command_name>.bin (SOF-wrapped script payload)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/os"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"
CMD_NAME="${1:-ls}"

build_os_command_inner() {
  CMD_DIR="user/os_commands"
  CMD_FILE="$CMD_DIR/$CMD_NAME.cmd"
  
  test -f "$CMD_FILE" || {
    echo "Error: $CMD_FILE not found"
    exit 1
  }

  mkdir -p "$OUT_DIR"

  python3 tools/build_script_elf.py "$CMD_FILE" "artifacts/os/$CMD_NAME.elf"

  # Build sof_wrap if not already built
  if [ ! -f "tools/sof_wrap/sof_wrap" ]; then
    make -C tools/sof_wrap
  fi

  # Wrap ELF in SOF container
  ./tools/sof_wrap/sof_wrap \
    --type bin --name "$CMD_NAME" --author "SecureOS" --version "1.0.0" \
    --date "$(date -u +%Y-%m-%d)" \
    "artifacts/os/$CMD_NAME.elf" "artifacts/os/$CMD_NAME.bin"
  
  echo "Built artifacts/os/$CMD_NAME.bin"
}

mkdir -p "$OUT_DIR"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    echo "Toolchain image not found, skipping OS command build"
    exit 0
  fi

  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" \
    bash -lc "set -euo pipefail; ./build/scripts/build_os_command.sh '$CMD_NAME'"
else
  build_os_command_inner
fi
