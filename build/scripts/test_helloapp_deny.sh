#!/usr/bin/env bash
set -euo pipefail

# HelloApp deny-path validator for BUILD_ROADMAP.md §5.2 (issue #92).
# Asserts that when the launcher manifest omits CAP_CONSOLE_WRITE, the
# HelloApp's banner write attempt is denied (CAP_ERR_MISSING), no banner
# bytes are produced, and the structured marker
#   TEST:PASS:helloapp_denied_console_write
# appears on stdout, alongside an audit deny event for the attempt.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/tests/helloapp_deny_test.c" \
  -o "$OUT_DIR/helloapp_deny_test"

LOG_PATH="$OUT_DIR/helloapp_deny_test.log"
"$OUT_DIR/helloapp_deny_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:helloapp_denied_console_write" "$LOG_PATH"
grep -q "TEST:PASS:helloapp_deny" "$LOG_PATH"
# Defensive: the deny path must NOT print the HelloApp banner.
if grep -q "HelloApp: secureos M2 banner" "$LOG_PATH"; then
  echo "TEST:FAIL:helloapp_deny:banner_leaked_on_deny_path" >&2
  exit 1
fi
