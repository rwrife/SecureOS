#!/usr/bin/env bash
set -euo pipefail

# sosh capability allow-path validator (first enforcement slice of #351).
# Asserts that the soshlib evaluator consults the embedder cap-check
# callback before executing `echo` and, when allowed, emits the text and
# sets $? = 0 per docs/abi/sosh-capability-contract.md §4/§6.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_allow_test.c" \
  -o "$OUT_DIR/sosh_cap_allow_test"

LOG_PATH="$OUT_DIR/sosh_cap_allow_test.log"
"$OUT_DIR/sosh_cap_allow_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:sosh_cap_allow:echo_emits"        "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_allow:cap_check_invoked" "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_allow:exit_code_zero"    "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_allow"                   "$LOG_PATH"
