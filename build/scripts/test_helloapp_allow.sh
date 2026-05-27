#!/usr/bin/env bash
set -euo pipefail

# HelloApp allow-path validator for BUILD_ROADMAP.md §5.2 (issue #92).
# Asserts that when the launcher grants CAP_CONSOLE_WRITE to the HelloApp
# subject, the HelloApp banner is emitted (i.e., the gate returns CAP_OK
# with non-zero bytes_written) and the structured marker
#   TEST:PASS:helloapp_allowed_console_write
# appears on stdout.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/tests/helloapp_allow_test.c" \
  -o "$OUT_DIR/helloapp_allow_test"

LOG_PATH="$OUT_DIR/helloapp_allow_test.log"
"$OUT_DIR/helloapp_allow_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:helloapp_allowed_console_write" "$LOG_PATH"
grep -q "TEST:PASS:helloapp_allow" "$LOG_PATH"
