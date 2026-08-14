#!/usr/bin/env bash
# @file test_process_spawn_argv_roundtrip.sh
# @brief Issue #546 — host-side argv + exit-status contract pin for
#        `os_process_spawn`.
#
# Compiles tests/process_spawn_argv_roundtrip_test.c with the user runtime
# stubs and runs it. The test maps a synthetic native bridge page so the
# wrapper can be driven dynamically on host (including argv join behavior,
# the explicit v0 join-collision pin tracked by #724, and out_exit_status
# propagation).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/process_spawn_argv_roundtrip_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/process_spawn_argv_roundtrip_test"

LOG_PATH="$OUT_DIR/process_spawn_argv_roundtrip_test.log"
"$OUT_DIR/process_spawn_argv_roundtrip_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:process_spawn_argv_roundtrip:argv_n3_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:argv_n5_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:space_join_limitation_pinned" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:space_join_collision_pinned" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:space_join_multiarg_payload_pinned" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:space_join_multiarg_collision_pinned" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip:out_exit_status_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:process_spawn_argv_roundtrip$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
