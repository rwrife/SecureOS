#!/usr/bin/env bash
set -euo pipefail

# sosh capability write/append enforcement validator (fifth enforcement
# slice of #351; echo landed in #358, source/extcmd in #367, exists in
# #379, cat/ls in #381). Asserts that the soshlib evaluator routes
# `write <path> <content>` and `append <path> <content>` external
# commands through SOSH_CAP_FS_WRITE with resource=<path>, per
# docs/abi/sosh-capability-contract.md §4 / §6.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_write_append_test.c" \
  -o "$OUT_DIR/sosh_cap_write_append_test"

LOG_PATH="$OUT_DIR/sosh_cap_write_append_test.log"
"$OUT_DIR/sosh_cap_write_append_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_cap_write_append:write_allow_cap_id_is_fs_write" \
  "TEST:PASS:sosh_cap_write_append:write_allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_write_append:write_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_write_append:write_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_write_append:write_deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_write_append:write_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_write_append:write_deny_script_continues" \
  "TEST:PASS:sosh_cap_write_append:append_allow_cap_id_is_fs_write" \
  "TEST:PASS:sosh_cap_write_append:append_allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_write_append:append_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_write_append:append_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_write_append:append_deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_write_append:append_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_write_append:other_extcmd_still_uses_app_exec" \
  "TEST:PASS:sosh_cap_write_append:legacy_null_cap_check_noop" \
  "TEST:PASS:sosh_cap_write_append"; do
  grep -q "$marker" "$LOG_PATH"
done
