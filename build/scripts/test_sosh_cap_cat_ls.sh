#!/usr/bin/env bash
set -euo pipefail

# sosh capability cat/ls enforcement validator (fourth enforcement
# slice of #351; echo landed in #358, source/extcmd in #367, exists in
# #379). Asserts that the soshlib evaluator routes `cat <path>` and
# `ls <path>` external commands through SOSH_CAP_FS_READ with
# resource=<path>, per docs/abi/sosh-capability-contract.md §4 / §6.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_cat_ls_test.c" \
  -o "$OUT_DIR/sosh_cap_cat_ls_test"

LOG_PATH="$OUT_DIR/sosh_cap_cat_ls_test.log"
"$OUT_DIR/sosh_cap_cat_ls_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_id_is_fs_read" \
  "TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_cat_ls:cat_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_cat_ls:cat_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_cat_ls:cat_deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_cat_ls:cat_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_cat_ls:cat_deny_script_continues" \
  "TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_id_is_fs_read" \
  "TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_cat_ls:ls_allow_invokes_exec" \
  "TEST:PASS:sosh_cap_cat_ls:ls_deny_blocks_exec" \
  "TEST:PASS:sosh_cap_cat_ls:ls_deny_no_output_leaked" \
  "TEST:PASS:sosh_cap_cat_ls:ls_deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_cat_ls:other_extcmd_still_uses_app_exec" \
  "TEST:PASS:sosh_cap_cat_ls:legacy_null_cap_check_noop" \
  "TEST:PASS:sosh_cap_cat_ls"; do
  grep -q "$marker" "$LOG_PATH"
done
