#!/usr/bin/env bash
# Build + run the M1 syscall entry stub conformance test (issue #232).
#
# Covers:
#   - Every vector in [SYSCALL_VECTOR_BASE, SYSCALL_VECTOR_LIMIT) plus
#     a handful of out-of-range probes returns IPC_ERR_INVALID_MSG.
#   - kernel_syscall_entry emits the canonical
#     CAP:DENY:<actor>:syscall:- marker via the shared
#     cap_deny_marker formatter (single source of truth — issue #211).
#   - SYSCALL_ENTRY_ABI_ANCHOR cross-checks against OS_ABI_VERSION
#     from user/include/secureos_abi.h (same anchor pattern as #228).
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/proc/syscall_entry.c" \
  "$ROOT_DIR/tests/syscall_entry_stub_test.c" \
  -o "$OUT_DIR/syscall_entry_stub_test"

LOG_PATH="$OUT_DIR/syscall_entry_stub_test.log"
"$OUT_DIR/syscall_entry_stub_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:syscall_entry_stub_invalid_msg_sweep" "$LOG_PATH"
grep -q "TEST:PASS:syscall_entry_stub_deny_marker_shape" "$LOG_PATH"
grep -q "TEST:PASS:syscall_entry_stub_abi_anchor" "$LOG_PATH"
grep -q "TEST:PASS:syscall_entry_stub$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
