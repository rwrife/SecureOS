#!/usr/bin/env bash
# @file test_process_exit_qemu.sh
# @brief Issue #551 starter gate: bridge-level round-trip for
#        `os_process_exit` status capture + `os_process_spawn`
#        out_exit_status propagation.
#
# Compiles tests/process_exit_qemu_test.c with the user runtime stubs and
# runs it. The test maps a synthetic native bridge page so the wrapper path
# is exercised dynamically on host while the full launcher/QEMU harness is
# in progress.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/process_exit_qemu_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/process_exit_qemu_test"

LOG_PATH="$OUT_DIR/process_exit_qemu_test.log"
"$OUT_DIR/process_exit_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:process_exit_qemu:exit_bridge_invoked_0x42" "$LOG_PATH"
grep -q "TEST:PASS:process_exit_qemu:roundtrip_status_0x42" "$LOG_PATH"
grep -q "TEST:PASS:process_exit_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
