#!/usr/bin/env bash
# build/scripts/test_launcher_ownership_role_manifest_edges.sh
#
# Build + run issue #585 host acceptance for launcher ownership-role edge
# registration. The fixture asserts:
#   - owner/delegate roles are parented under launcher root and therefore
#     revoked by a launcher-root subtree cascade
#   - none role remains sentinel-rooted and survives that unrelated cascade

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
  "$ROOT_DIR/tests/harness/svc_subjects.c" \
  "$ROOT_DIR/tests/m5_ownership_role_manifest_edges_test.c" \
  -o "$OUT_DIR/m5_ownership_role_manifest_edges_test"

LOG_PATH="$OUT_DIR/m5_ownership_role_manifest_edges_test.log"
"$OUT_DIR/m5_ownership_role_manifest_edges_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:launcher_ownership_role_owner_registers_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_delegate_registers_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_none_registers_no_edge$" "$LOG_PATH"
grep -q "TEST:PASS:launcher_ownership_role_manifest_edges$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
