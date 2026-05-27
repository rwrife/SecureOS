#!/usr/bin/env bash
set -euo pipefail

# sosh capability deny-path validator (first enforcement slice of #351).
# Asserts that the soshlib evaluator short-circuits `echo` on deny, leaks
# no text, propagates the deny rc into $?, and does NOT abort the script
# (per docs/abi/sosh-capability-contract.md §6).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/soshlib/sosh_builtins.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_lexer.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_vars.c" \
  "$ROOT_DIR/user/libs/soshlib/sosh_eval.c" \
  "$ROOT_DIR/tests/sosh_cap_deny_test.c" \
  -o "$OUT_DIR/sosh_cap_deny_test"

LOG_PATH="$OUT_DIR/sosh_cap_deny_test.log"
"$OUT_DIR/sosh_cap_deny_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:sosh_cap_deny:no_output_leaked"     "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_deny:exit_code_propagates" "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_deny:script_continues"     "$LOG_PATH"
grep -q "TEST:PASS:sosh_cap_deny"                      "$LOG_PATH"

# Defense in depth: no `this-must-not-appear` substring on the log,
# confirming the gated echo did not leak via stdout either.
if grep -q "this-must-not-appear" "$LOG_PATH"; then
  echo "TEST:FAIL:sosh_cap_deny:text_leaked_to_log" >&2
  exit 1
fi
