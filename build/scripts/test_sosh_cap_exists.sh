#!/usr/bin/env bash
set -euo pipefail

# sosh capability `exists` enforcement validator (third enforcement
# slice of #351). Asserts that the soshlib evaluator gates the
# `if exists <path>` conditional builtin on SOSH_CAP_FS_READ per
# docs/abi/sosh-capability-contract.md §4 / §6 (allow runs the probe
# and evaluates the THEN branch; deny short-circuits the probe, the
# conditional evaluates false, $? carries the embedder rc, and the
# script continues past the if/end block).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_exists_test.c" \
  -o "$OUT_DIR/sosh_cap_exists_test"

LOG_PATH="$OUT_DIR/sosh_cap_exists_test.log"
"$OUT_DIR/sosh_cap_exists_test" | tee "$LOG_PATH"

for marker in \
  "TEST:PASS:sosh_cap_exists:allow_cap_resource_is_path" \
  "TEST:PASS:sosh_cap_exists:allow_invokes_exec" \
  "TEST:PASS:sosh_cap_exists:allow_condition_true" \
  "TEST:PASS:sosh_cap_exists:deny_blocks_exec" \
  "TEST:PASS:sosh_cap_exists:deny_condition_false" \
  "TEST:PASS:sosh_cap_exists:deny_exit_code_propagates" \
  "TEST:PASS:sosh_cap_exists:deny_script_continues" \
  "TEST:PASS:sosh_cap_exists:legacy_null_cap_check_no_op" \
  "TEST:PASS:sosh_cap_exists"; do
  grep -q "$marker" "$LOG_PATH"
done

echo "test_sosh_cap_exists OK"
