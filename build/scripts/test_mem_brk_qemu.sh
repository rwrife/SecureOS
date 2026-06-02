#!/usr/bin/env bash
# build/scripts/test_mem_brk_qemu.sh
#
# Build + run the `_qemu`-tier end-to-end round-trip for the
# os_mem_brk bridge slot (issue #495, follow-up to #421 / PR #455).
#
# Drives the production app_native_mem_brk implementation extracted
# into kernel/user/app_native_heap.c so the launcher and this test
# share the same `(int delta, void **out_prev_break) -> int` body
# the bridge wires into `bridge->mem_brk`.
#
# Asserted markers (single source of truth pinned across
# tests/mem_brk_qemu_test.c, this script, and the issue body):
#   TEST:PASS:mem_brk_qemu:grow
#   TEST:PASS:mem_brk_qemu:shrink
#   TEST:PASS:mem_brk_qemu:over_cap_denied
#   TEST:PASS:mem_brk_qemu:arena_reset
#   TEST:PASS:mem_brk_qemu
#
# Pure host build (no QEMU required) — same convention as every
# other `_qemu` peer (`tests/m2_helloapp_*_qemu_test.c`,
# `tests/m3_fs_*_qemu_test.c`, `tests/m4_broker_*_qemu_test.c`).
# The "_qemu" suffix follows BUILD_ROADMAP §5.2: the test drives a
# real substrate code path (here: the live brk arena), not an
# in-process fast fixture.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/user/app_native_heap.c" \
  "$ROOT_DIR/tests/mem_brk_qemu_test.c" \
  -o "$OUT_DIR/mem_brk_qemu_test"

LOG_PATH="$OUT_DIR/mem_brk_qemu_test.log"
"$OUT_DIR/mem_brk_qemu_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:mem_brk_qemu:grow" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_qemu:shrink" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_qemu:over_cap_denied" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_qemu:arena_reset" "$LOG_PATH"
grep -q "TEST:PASS:mem_brk_qemu$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
# Belt-and-braces: the brk path never panics; serial log must not
# mention kernel panic or cap_table corruption (parity with the
# m5_owner_delete_cascade_*_qemu peer's defensive grep).
! grep -qE "kernel panic|cap_table:FAIL" "$LOG_PATH"
