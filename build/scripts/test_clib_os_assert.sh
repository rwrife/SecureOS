#!/usr/bin/env bash
# build/scripts/test_clib_os_assert.sh
#
# Build + run the host-side smoke for the `clib_os_assert` forwarder
# (M7-TOOLCHAIN-004 follow-up, issue #407; on-target sibling of the
# freestanding <clib/assert.h> nucleus shipped by PR #443).
#
# Mirrors `test_clib_os_brk.sh` and `test_clib_assert.sh`:
#   - compile the freestanding nucleus + the on-target forwarder +
#     the user-runtime stubs + the test
#   - run it, tee the log, grep the deterministic TEST:PASS markers,
#     fail on any TEST:FAIL line.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/user/libs/clib/src/assert.c" \
  "$ROOT_DIR/user/libs/clib/src/os_assert.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  "$ROOT_DIR/tests/clib_os_assert_test.c" \
  -o "$OUT_DIR/clib_os_assert_test"

LOG_PATH="$OUT_DIR/clib_os_assert_test.log"
"$OUT_DIR/clib_os_assert_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_os_assert:forwarder_signature_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_os_assert:install_symbol_pinned"      "$LOG_PATH"
grep -q "TEST:PASS:clib_os_assert:setter_accepts_forwarder"   "$LOG_PATH"
grep -q "TEST:PASS:clib_os_assert$"                           "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
