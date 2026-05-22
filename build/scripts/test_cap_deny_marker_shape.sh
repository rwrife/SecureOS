#!/usr/bin/env bash
# Build + run the capability-deny marker shape conformance test (issue #211).
#
# Single source of truth for the CAP:DENY:<...> serial marker grammar
# defined in docs/abi/capability-deny-contract.md §4. Future deny-path
# services (M3 fs #108, M4 broker #115, M1 IPC #210) reuse the formatter
# in kernel/cap/cap_deny_marker.c rather than inventing per-service grep.
#
# Outputs deterministic TEST:PASS markers consumed by build/scripts/test.sh
# and validate_bundle.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/cap_deny_marker.c" \
  "$ROOT_DIR/tests/cap_deny_marker_shape_test.c" \
  -o "$OUT_DIR/cap_deny_marker_shape_test"

LOG_PATH="$OUT_DIR/cap_deny_marker_shape_test.log"
"$OUT_DIR/cap_deny_marker_shape_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:cap_deny_marker_shape_format_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:cap_deny_marker_shape_drivers" "$LOG_PATH"
grep -q "TEST:PASS:cap_deny_marker_shape_negative" "$LOG_PATH"
grep -q "TEST:PASS:cap_deny_marker_shape_cap_table" "$LOG_PATH"
grep -q "TEST:PASS:cap_deny_marker_shape" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
