#!/usr/bin/env bash
# build/scripts/test_launcher_ownership_role_manifest_edges.sh
#
# Host gate scaffold for issue #585 (M5 ownership-role runtime wiring).
# Compiles and executes the marker fixture that pins the ownership-role
# launcher edge-registration marker IDs prior to runtime implementation.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/tests/m5_ownership_role_manifest_edges_test.c" \
  -o "$OUT_DIR/m5_ownership_role_manifest_edges_test"

LOG_PATH="$OUT_DIR/m5_ownership_role_manifest_edges_test.log"
"$OUT_DIR/m5_ownership_role_manifest_edges_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_ownership_role_owner_registers_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_delegate_registers_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_none_registers_no_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_manifest_edges$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"