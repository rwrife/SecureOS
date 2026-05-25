#!/usr/bin/env bash
# scripts/boot.sh - Host-side QEMU launcher (macOS/Linux)
#
# Launches QEMU on the host with the SecureOS ISO and disk image.
# QEMU runs on the host (not in Docker) for display/device access.
#
# Usage: scripts/boot.sh [console|graphics]  (default: console)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODE="${1:-console}"

ISO_PATH="$ROOT_DIR/artifacts/kernel/secureos.iso"
DISK_PATH="$ROOT_DIR/artifacts/disk/secureos-disk.img"

# Verify artifacts exist
if [[ ! -f "$ISO_PATH" ]]; then
  echo "ERROR: Kernel ISO not found at $ISO_PATH"
  echo "Run ./scripts/build.sh first."
  exit 1
fi

if [[ ! -f "$DISK_PATH" ]]; then
  echo "ERROR: Disk image not found at $DISK_PATH"
  echo "Run ./scripts/build.sh first."
  exit 1
fi

# Verify QEMU is available
if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
  echo "ERROR: qemu-system-x86_64 not found in PATH."
  echo "Run the appropriate setup script to install QEMU."
  exit 1
fi

# Stop any existing SecureOS QEMU instances
if command -v pkill >/dev/null 2>&1; then
  pkill -f "qemu-system-x86_64.*secureos-disk.img" >/dev/null 2>&1 || true
  pkill -f "qemu-system-x86_64.*secureos.iso" >/dev/null 2>&1 || true
fi

# Common QEMU arguments
QEMU_COMMON=(
  -cdrom "$ISO_PATH"
  -boot d
  -drive "format=raw,file=$DISK_PATH,if=ide,index=0,media=disk"
  -device isa-debug-exit,iobase=0xf4,iosize=0x04
  -serial stdio
  -monitor none
  -m 256M
  -smp 1
  -no-reboot
  -rtc base=utc,clock=host
  -netdev user,id=net0
  -device virtio-net-pci,disable-modern=on,netdev=net0
)

case "$MODE" in
  console)
    echo "Booting SecureOS (serial console mode)..."
    echo "Type commands at the secureos> prompt. Use 'exit pass' to stop QEMU."
    echo ""
    set +e
    qemu-system-x86_64 "${QEMU_COMMON[@]}" -nographic
    RC=$?
    set -e
    ;;
  graphics)
    echo "Booting SecureOS (graphics mode)..."
    echo "A QEMU window will open with the VGA display."
    echo "Serial I/O is still connected to this terminal."
    echo ""
    set +e
    qemu-system-x86_64 "${QEMU_COMMON[@]}"
    RC=$?
    set -e
    ;;
  *)
    echo "Unknown mode: $MODE"
    echo "Usage: scripts/boot.sh [console|graphics]"
    exit 1
    ;;
esac

# Exit code 33 = debug exit with pass code (0x10 << 1 | 1)
if [[ "$RC" -eq 33 ]]; then
  echo ""
  echo "SecureOS exited cleanly (PASS)."
  exit 0
fi

exit "$RC"
