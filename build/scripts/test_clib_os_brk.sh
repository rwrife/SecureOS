#!/usr/bin/env bash
# build/scripts/test_clib_os_brk.sh
#
# Build + run the host-side smoke for the `clib_os_brk` forwarder
# (M7-TOOLCHAIN-001 slice 3, issue #421).
#
# Mirrors `test_mem_brk_wrapper.sh` and `test_clib_malloc.sh`:
#   - compile the forwarder source + the user-runtime stubs + the test
#   - run it, tee the log, grep the deterministic TEST:PASS markers,
#     fail on any TEST:FAIL line.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/user/libs/clib/src/os_brk.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  "$ROOT_DIR/tests/clib_os_brk_test.c" \
  -o "$OUT_DIR/clib_os_brk_test"

LOG_PATH="$OUT_DIR/clib_os_brk_test.log"
"$OUT_DIR/clib_os_brk_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_os_brk:signature_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_os_brk:zero_delta_rejected" "$LOG_PATH"
grep -q "TEST:PASS:clib_os_brk:overflow_rejected" "$LOG_PATH"
grep -q "TEST:PASS:clib_os_brk$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
