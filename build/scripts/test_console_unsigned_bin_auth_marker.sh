#!/usr/bin/env bash
# @file test_console_unsigned_bin_auth_marker.sh
# @brief Build + run issue #542 host gate that pins the unsigned-binary
#        authorization marker literals and AUTH_TYPE_UNSIGNED_BIN constant.
#
# Compiles tests/console_unsigned_bin_auth_marker_test.c and asserts the
# emitted TEST:PASS markers so marker/constant drift flips CI red.
#
# Launched by:
#   build/scripts/test.sh console_unsigned_bin_auth_marker

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/tests/console_unsigned_bin_auth_marker_test.c" \
  -o "$OUT_DIR/console_unsigned_bin_auth_marker_test"

LOG_PATH="$OUT_DIR/console_unsigned_bin_auth_marker_test.log"
cd "$ROOT_DIR"
"$OUT_DIR/console_unsigned_bin_auth_marker_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:console_unsigned_bin_auth_marker:cached_header" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:path_line" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:decision_allow_cached" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:decision_deny_cached" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:decision_allow_prompt" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:decision_deny_prompt" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker:auth_type_unsigned_bin_constant" "$LOG_PATH"
grep -q "TEST:PASS:console_unsigned_bin_auth_marker$" "$LOG_PATH"
