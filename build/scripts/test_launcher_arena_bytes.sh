#!/usr/bin/env bash
# build/scripts/test_launcher_arena_bytes.sh
#
# Build + run the M7-TOOLCHAIN-001 slice 3 acceptance test
# (issue #448, refs #404 / #421 / #424).
#
# Covers:
#   - launcher_spawn_app_from_manifest() resolves a missing
#     `runtime.arena_bytes` to PROC_ARENA_BYTES_DEFAULT (legacy parity).
#   - A declared value in `[DEFAULT, MAX]` is the active per-spawn cap
#     reported by launcher_arena_active_cap().
#   - A declared value above PROC_ARENA_BYTES_MAX (16 MiB) is rejected
#     with LAUNCHER_ERR_INVALID_MANIFEST + a deny-audit event with
#     reason LAUNCHER_ARENA_DENY_OVER_CAP. Kernel must not panic.
#   - A declared non-zero value below PROC_ARENA_BYTES_DEFAULT (64 KiB)
#     is rejected with the same shape and reason
#     LAUNCHER_ARENA_DENY_UNDER_FLOOR.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/capability.c" \
  "$ROOT_DIR/kernel/cap/cap_table.c" \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/kernel/cap/cap_gate.c" \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/kernel/proc/address_space.c" \
  "$ROOT_DIR/kernel/proc/process.c" \
  "$ROOT_DIR/kernel/user/launcher.c" \
  "$ROOT_DIR/tests/harness/m2_subjects.c" \
  "$ROOT_DIR/tests/launcher_arena_bytes_test.c" \
  -o "$OUT_DIR/launcher_arena_bytes_test"

LOG_PATH="$OUT_DIR/launcher_arena_bytes_test.log"
"$OUT_DIR/launcher_arena_bytes_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_arena_bytes:default_when_omitted_matches_legacy" "$LOG_PATH"
grep -q "TEST:PASS:launcher_arena_bytes:declared_value_applied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_arena_bytes:over_cap_denied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_arena_bytes:under_floor_denied" "$LOG_PATH"
grep -q "TEST:PASS:launcher_arena_bytes$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
