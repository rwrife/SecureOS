#!/usr/bin/env bash
# build/scripts/test_mem_brk_arena_cap_deny.sh
#
# Build + run the host-side deny-marker gate for `os_mem_brk`
# over-cap growth (issue #558).
#
# Asserts:
#   - positive growth beyond APP_NATIVE_HEAP_BYTES returns OS_STATUS_DENIED
#   - over-cap deny path emits canonical CAP:DENY mem_brk marker evidence
#   - shrink-underflow deny stays silent (no over-cap marker leakage)
#
# Deterministic markers (consumed by test.sh + validate_bundle.sh):
#   TEST:PASS:mem_brk_arena_cap_deny:deny_status
#   TEST:PASS:mem_brk_arena_cap_deny:deny_marker
#   TEST:PASS:mem_brk_arena_cap_deny:shrink_underflow_silent
#   TEST:PASS:mem_brk_arena_cap_deny

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/user/app_native_heap.c" \
  "$ROOT_DIR/tests/mem_brk_arena_cap_deny_test.c" \
  -o "$OUT_DIR/mem_brk_arena_cap_deny_test"

LOG_PATH="$OUT_DIR/mem_brk_arena_cap_deny_test.log"
"$OUT_DIR/mem_brk_arena_cap_deny_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:mem_brk_arena_cap_deny:deny_status" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_arena_cap_deny:deny_marker" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_arena_cap_deny:shrink_underflow_silent" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_arena_cap_deny$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
