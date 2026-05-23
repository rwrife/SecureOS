#!/usr/bin/env bash
# build/scripts/test_aspace_invariant.sh
#
# Build + run the M1 address-space layout-invariant unit test
# (issue #260, scheduler block/wake half — the kernel-internal
# panic predicate `aspace_invariant_ok()`).
#
# Covers:
#   - Allow: every window produced by aspace_partition() passes.
#   - Allow: hand-built window with NULL ipc_scratch passes.
#   - Deny: NULL, zero-size, base+size overflow, stack_top below
#     base, stack_top past window end.
#   - Deny: ipc_scratch outside the window or whose IPC_MSG_PAYLOAD_MAX
#     span straddles the upper boundary.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/tests/aspace_invariant_test.c" \
  -o "$OUT_DIR/aspace_invariant_test"

LOG_PATH="$OUT_DIR/aspace_invariant_test.log"
"$OUT_DIR/aspace_invariant_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:aspace_invariant_allow_partitioned" "$LOG_PATH"
grep -q "TEST:PASS:aspace_invariant_allow_no_scratch" "$LOG_PATH"
grep -q "TEST:PASS:aspace_invariant_deny_layout" "$LOG_PATH"
grep -q "TEST:PASS:aspace_invariant_deny_scratch_escapes" "$LOG_PATH"
grep -q "TEST:PASS:aspace_invariant$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
