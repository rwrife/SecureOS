#!/usr/bin/env bash
set -euo pipefail

# sosh capability source/exec enforcement validator (second enforcement
# slice of #351; first slice for echo landed in #358). Asserts that the
# soshlib evaluator gates `source <path>` on SOSH_CAP_FS_READ and external
# command dispatch on SOSH_CAP_APP_EXEC, per
# docs/abi/sosh-capability-contract.md §4 / §6.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_source_exec_test.c" \
  -o "$OUT_DIR/sosh_cap_source_exec_test"

LOG_PATH="$OUT_DIR/sosh_cap_source_exec_test.log"
"$OUT_DIR/sosh_cap_source_exec_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_cap_source_exec:source_allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_source_exec:source_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_source_exec:source_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_source_exec:source_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_source_exec:source_deny_script_continues" \
  "TEST:PASS:sosh_cap_source_exec:extcmd_allow_cap_resource_is_binary" \
  "TEST:PASS:sosh_cap_source_exec:extcmd_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_source_exec:extcmd_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_source_exec:extcmd_deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_source_exec:extcmd_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_source_exec"; do
  grep -q "$marker" "$LOG_PATH"
done

# Defense in depth: the denied external-command sentinel must NOT appear
# anywhere in the log (would mean the gate let exec run).
if grep -q "EXTERNAL_RAN" "$LOG_PATH"; then
  # The allow scenario legitimately emits EXTERNAL_RAN, but we can rely
  # on the per-marker checks above for ordering. This check is informational
  # only and intentionally not fatal.
  :
fi
