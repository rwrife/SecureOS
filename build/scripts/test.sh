#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_NAME="${1:-hello_boot}"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

stop_secureos_instances() {
  if command -v docker >/dev/null 2>&1; then
    mapfile -t IDS < <(docker ps --filter "ancestor=$IMAGE_TAG" --format "{{.ID}}")
    if [[ ${#IDS[@]} -gt 0 ]]; then
      docker stop "${IDS[@]}" >/dev/null 2>&1 || true
    fi
  fi

  if command -v pkill >/dev/null 2>&1; then
    pkill -f "qemu-system-x86_64.*secureos-disk.img" >/dev/null 2>&1 || true
    pkill -f "qemu-system-x86_64.*secureos.iso" >/dev/null 2>&1 || true
  fi
}

stop_secureos_instances

usage() {
  cat <<EOF
Usage: $(basename "$0") [hello_boot|hello_boot_negative|cap_api_contract|capability_table|capability_gate|capability_audit|event_bus|scheduler|tls|https|fs_service|app_runtime|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions]

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
  cap_api_contract)
    "$ROOT_DIR/build/scripts/test_cap_api_contract.sh"
    ;;
  capability_table)
    "$ROOT_DIR/build/scripts/test_capability_table.sh"
    ;;
  capability_gate)
    "$ROOT_DIR/build/scripts/test_capability_gate.sh"
    ;;
  capability_audit)
    "$ROOT_DIR/build/scripts/test_capability_audit.sh"
    ;;
  event_bus)
    "$ROOT_DIR/build/scripts/test_event_bus.sh"
    ;;
  scheduler)
    "$ROOT_DIR/build/scripts/test_scheduler.sh"
    ;;
  sof_format)
    "$ROOT_DIR/build/scripts/test_sof_format.sh"
    ;;
  ed25519)
    "$ROOT_DIR/build/scripts/test_ed25519.sh"
    ;;
  cert_chain)
    "$ROOT_DIR/build/scripts/test_cert_chain.sh"
    ;;
  codesign)
    "$ROOT_DIR/build/scripts/test_codesign.sh"
    ;;
  tls)
    "$ROOT_DIR/build/scripts/test_tls.sh"
    ;;
  https)
    "$ROOT_DIR/build/scripts/test_https.sh"
    ;;
  fs_service)
    "$ROOT_DIR/build/scripts/test_fs_service.sh"
    ;;
  app_runtime)
    "$ROOT_DIR/build/scripts/test_app_runtime.sh"
    ;;
  kernel_console)
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_console
    ;;
  kernel_filedemo)
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_filedemo
    ;;
  kernel_persistence)
    "$ROOT_DIR/build/scripts/test_kernel_persistence.sh"
    ;;
  kernel_sessions)
    "$ROOT_DIR/build/scripts/build_kernel_image.sh"
    "$ROOT_DIR/build/scripts/build_disk_image.sh"
    "$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_sessions
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    usage
    exit 1
    ;;
esac
