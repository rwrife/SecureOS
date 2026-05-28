#!/usr/bin/env bash
set -euo pipefail

# sosh capability export enforcement validator (sixth enforcement
# slice of #351; echo landed in #358, source/extcmd in #367, exists
# in #379, cat/ls in #381, write/append in #382). Asserts that the
# soshlib evaluator routes the `export VAR=value` builtin's env-service
# write through SOSH_CAP_ENV_WRITE with resource=<var>, per
# docs/abi/sosh-capability-contract.md §4 / §6.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_export_test.c" \
  -o "$OUT_DIR/sosh_cap_export_test"

LOG_PATH="$OUT_DIR/sosh_cap_export_test.log"
"$OUT_DIR/sosh_cap_export_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_cap_export:allow_cap_id_is_env_write" \
  "TEST:PASS:sosh_cap_export:allow_cap_resource_is_var_name" \
  "TEST:PASS:sosh_cap_export:allow_invokes_env_exec" \
  "TEST:PASS:sosh_cap_export:allow_exec_payload_is_var_eq_value" \
  "TEST:PASS:sosh_cap_export:allow_exit_code_zero" \
  "TEST:PASS:sosh_cap_export:deny_blocks_env_exec" \
  "TEST:PASS:sosh_cap_export:deny_var_still_set_in_script" \
  "TEST:PASS:sosh_cap_export:deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_export:deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_export:deny_script_continues" \
  "TEST:PASS:sosh_cap_export:set_unaffected_by_env_write_deny" \
  "TEST:PASS:sosh_cap_export:legacy_null_cap_check_noop" \
  "TEST:PASS:sosh_cap_export"; do
  grep -q "$marker" "$LOG_PATH"
done
