#!/usr/bin/env bash
# @file test_sosh_external_exec.sh
# @brief Issue #493 — sosh fall-through to os_process_spawn host smoke.
#
# Compiles `tests/sosh_external_exec_test.c` (which #includes the
# embedder helper directly so the test is hermetic w.r.t. soshlib and
# the bridge) and asserts every PASS marker is present in the output.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/sosh_external_exec_test.c" \
  -o "$OUT_DIR/sosh_external_exec_test"

LOG_PATH="$OUT_DIR/sosh_external_exec_test.log"
"$OUT_DIR/sosh_external_exec_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_external_exec:allow_spawn_called" \
  "TEST:PASS:sosh_external_exec:allow_probe_order_apps_first" \
  "TEST:PASS:sosh_external_exec:allow_exit_status_propagates" \
  "TEST:PASS:sosh_external_exec:allow_argv_passes_command_and_args" \
  "TEST:PASS:sosh_external_exec:deny_returns_nonzero_no_swallow" \
  "TEST:PASS:sosh_external_exec:deny_marker_owner_is_kernel" \
  "TEST:PASS:sosh_external_exec:deny_bare_name_preserves_cap_deny_marker" \
  "TEST:PASS:sosh_external_exec:unknown_no_spawn_attempt" \
  "TEST:PASS:sosh_external_exec:unknown_returns_not_found_sentinel" \
  "TEST:PASS:sosh_external_exec:absolute_path_single_probe" \
  "TEST:PASS:sosh_external_exec"; do
  grep -q "$marker" "$LOG_PATH"
done

# Issue #575 canary: bare-name fallback deny path must preserve the
# kernel CAP:DENY line on the captured output stream.
grep -q "CAP:DENY:4242:app_exec:hello" "$LOG_PATH"
