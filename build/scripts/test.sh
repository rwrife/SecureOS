#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_NAME="${1:-hello_boot}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [hello_boot|hello_boot_negative]

Runs SecureOS test targets.
EOF
}

if [[ "$TEST_NAME" == "-h" || "$TEST_NAME" == "--help" ]]; then
  usage
  exit 0
fi

case "$TEST_NAME" in
  hello_boot)
    "$ROOT_DIR/build/scripts/test_boot_sector.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot
    ;;
  hello_boot_negative)
    "$ROOT_DIR/build/scripts/test_boot_sector_fail.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot_fail
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    usage
    exit 1
    ;;
esac
