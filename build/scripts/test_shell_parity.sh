#!/usr/bin/env bash
#
# test_shell_parity.sh — validator wrapper around check_shell_parity.sh that
# emits TEST:START / TEST:PASS / TEST:FAIL lines consumable by test.sh and
# validate_bundle.sh. Tracked by SecureOS issue #156.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="parity"

echo "TEST:START:${TARGET}"

if bash "${ROOT_DIR}/build/scripts/check_shell_parity.sh"; then
  echo "TEST:PASS:${TARGET}:build_scripts_sh_ps1_in_sync"
  exit 0
else
  echo "TEST:FAIL:${TARGET}:shell_parity_drift_detected"
  exit 1
fi
