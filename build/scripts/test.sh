#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_NAME="${1:-hello_boot}"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

# Directory containing subordinate validator/build scripts. Overridable so the
# harness dispatch behavior can be exercised by a fixture without mutating the
# real tree. Defaults to the canonical build/scripts directory.
SCRIPTS_DIR="${SECUREOS_SCRIPTS_DIR:-$ROOT_DIR/build/scripts}"

# Distinct exit code reserved for harness-level dispatch failures (missing or
# unreadable subordinate scripts). Anything else from a child is treated as a
# normal test failure. Kept in EX_CONFIG range (78) to avoid colliding with
# common test exit codes.
HARNESS_ERROR_EXIT=78

# run_validator <relative_script> [args...]
#
# Invokes the named subordinate script via `bash <path>` so that a missing
# executable bit (see issue #90) cannot silently break CI: we only need the
# file to exist and be readable. If either precondition fails, we emit a
# deterministic, parseable marker on stderr and exit with HARNESS_ERROR_EXIT
# so callers (validate_bundle.sh, CI) can distinguish infra breakage from a
# real test regression.
run_validator() {
  local relpath="$1"
  shift || true
  local path="$SCRIPTS_DIR/$relpath"
  if [[ ! -e "$path" ]]; then
    echo "TEST:FAIL:harness_missing_script:$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  if [[ ! -r "$path" ]]; then
    echo "TEST:FAIL:harness_unreadable_script:$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  bash "$path" "$@"
}

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
Usage: $(basename "$0") [hello_boot|hello_boot_negative|cap_api_contract|capability_table|capability_gate|capability_audit|event_bus|scheduler|tls|https|fs_service|app_runtime|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions|harness_dispatch]

Runs SecureOS test targets.
EOF
}

if [[ "$TEST_NAME" == "-h" || "$TEST_NAME" == "--help" ]]; then
  usage
  exit 0
fi

case "$TEST_NAME" in
  hello_boot)
    run_validator test_boot_sector.sh
    run_validator run_qemu.sh --test hello_boot
    ;;
  hello_boot_negative)
    run_validator test_boot_sector_fail.sh
    run_validator run_qemu.sh --test hello_boot_fail
    ;;
  cap_api_contract)
    run_validator test_cap_api_contract.sh
    ;;
  capability_table)
    run_validator test_capability_table.sh
    ;;
  capability_gate)
    run_validator test_capability_gate.sh
    ;;
  capability_audit)
    run_validator test_capability_audit.sh
    ;;
  event_bus)
    run_validator test_event_bus.sh
    ;;
  scheduler)
    run_validator test_scheduler.sh
    ;;
  sof_format)
    run_validator test_sof_format.sh
    ;;
  ed25519)
    run_validator test_ed25519.sh
    ;;
  cert_chain)
    run_validator test_cert_chain.sh
    ;;
  codesign)
    run_validator test_codesign.sh
    ;;
  tls)
    run_validator test_tls.sh
    ;;
  https)
    run_validator test_https.sh
    ;;
  fs_service)
    run_validator test_fs_service.sh
    ;;
  app_runtime)
    run_validator test_app_runtime.sh
    ;;
  kernel_console)
    run_validator build_kernel_image.sh
    run_validator build_disk_image.sh
    run_validator run_qemu.sh --test kernel_console
    ;;
  kernel_filedemo)
    run_validator build_kernel_image.sh
    run_validator build_disk_image.sh
    run_validator run_qemu.sh --test kernel_filedemo
    ;;
  kernel_persistence)
    run_validator test_kernel_persistence.sh
    ;;
  kernel_sessions)
    run_validator build_kernel_image.sh
    run_validator build_disk_image.sh
    run_validator run_qemu.sh --test kernel_sessions
    ;;
  harness_dispatch)
    run_validator test_harness_dispatch.sh
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    usage
    exit 1
    ;;
esac
