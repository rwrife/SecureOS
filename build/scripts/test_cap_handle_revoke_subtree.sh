#!/usr/bin/env bash
# build/scripts/test_cap_handle_revoke_subtree.sh
#
# Compiles and runs tests/cap_handle_revoke_subtree_test.c against the
# M1-CAPTBL-004 reserved-symbol stub of cap_handle_revoke_subtree
# (issue #241, plan #197).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/tests/cap_handle_revoke_subtree_test.c" \
  -o "$OUT_DIR/cap_handle_revoke_subtree_test"

LOG_PATH="$OUT_DIR/cap_handle_revoke_subtree_test.log"
"$OUT_DIR/cap_handle_revoke_subtree_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:cap_handle_revoke_subtree_null_handle_invalid" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subtree_malformed_tag_invalid" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subtree_walks_chain" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subtree_null_parent_no_op" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subtree_stale_root_missing" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_grant_forwarder_legacy_callers_unaffected" "$LOG_PATH"
grep -q "TEST:PASS:cap_handle_revoke_subtree$" "$LOG_PATH"
