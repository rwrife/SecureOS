#!/usr/bin/env bash
# tests/harness/skip_backlog_cap_fixture_test.sh
#
# Fixture coverage for tools/check_skip_backlog_cap.py (issue #641).
# Verifies four policy paths:
#   1) open issue over cap without allowlist -> FAIL
#   2) open issue over cap with grandfathered ceiling -> PASS
#   3) open issue above grandfathered ceiling -> FAIL
#   4) stale allowlist entry once count drops to cap -> FAIL

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:skip_backlog_cap_fixture\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tests/m7_toolchain"

STATE_JSON="$SANDBOX/issue_states.json"
cat > "$STATE_JSON" <<'EOF'
{
  "409": "OPEN",
  "410": "OPEN",
  "411": "CLOSED"
}
EOF

write_markers() {
  local out_path="$1"
  local issue_409_count="$2"
  python3 - <<'PY' "$out_path" "$issue_409_count"
import json
import sys

out_path = sys.argv[1]
count_409 = int(sys.argv[2])
markers = []
for i in range(count_409):
    markers.append(
        {
            "name": f"toolchain_409_{i:02d}",
            "gatingIssue": 409,
            "reason": "awaiting_409",
            "description": "fixture"
        }
    )
for i in range(3):
    markers.append(
        {
            "name": f"toolchain_410_{i:02d}",
            "gatingIssue": 410,
            "reason": "awaiting_410",
            "description": "fixture"
        }
    )
for i in range(20):
    markers.append(
        {
            "name": f"toolchain_411_{i:02d}",
            "gatingIssue": 411,
            "reason": "awaiting_411",
            "description": "closed-issue fixture"
        }
    )
json.dump({"schemaVersion": 1, "markers": markers}, open(out_path, "w", encoding="utf-8"), indent=2)
print()
PY
}

write_allowlist() {
  local out_path="$1"
  local ceiling_409="$2"
  cat > "$out_path" <<EOF
{
  "schemaVersion": 1,
  "defaultCap": 12,
  "removeOnly": true,
  "grandfatheredOverages": [
    {
      "gatingIssue": 409,
      "maxPinnedHarnesses": $ceiling_409,
      "note": "fixture grandfathered overage"
    }
  ]
}
EOF
}

run_cap_check() {
  local markers_path="$1"
  local allowlist_path="$2"
  local stdout_path="$3"
  local stderr_path="$4"

  set +e
  python3 "$ROOT_DIR/tools/check_skip_backlog_cap.py" \
    --root "$SANDBOX" \
    --markers "$markers_path" \
    --allowlist "$allowlist_path" \
    --issue-states-json "$STATE_JSON" \
    > "$stdout_path" 2> "$stderr_path"
  local rc=$?
  set -e
  echo "$rc"
}

# Case 1: over-cap OPEN issue without allowlist entry => FAIL.
MARKERS1="$SANDBOX/tests/m7_toolchain/markers_case1.json"
ALLOW1="$SANDBOX/tests/m7_toolchain/allowlist_case1.json"
write_markers "$MARKERS1" 13
cat > "$ALLOW1" <<'EOF'
{
  "schemaVersion": 1,
  "defaultCap": 12,
  "removeOnly": true,
  "grandfatheredOverages": []
}
EOF
RC=$(run_cap_check "$MARKERS1" "$ALLOW1" "$TMP_DIR/c1.out" "$TMP_DIR/c1.err")
if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case1_unexpected_exit:%s\n' "$RC"
  cat "$TMP_DIR/c1.out"
  cat "$TMP_DIR/c1.err" >&2
  exit 1
fi
if ! grep -Fq 'SKIP_BACKLOG_CAP:FAIL:#409:open_issue_over_cap_without_allowlist:count=13:cap=12' "$TMP_DIR/c1.err"; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case1_missing_over_cap_marker\n'
  cat "$TMP_DIR/c1.err" >&2
  exit 1
fi

# Case 2: same count with grandfathered ceiling => PASS.
ALLOW2="$SANDBOX/tests/m7_toolchain/allowlist_case2.json"
write_allowlist "$ALLOW2" 13
RC=$(run_cap_check "$MARKERS1" "$ALLOW2" "$TMP_DIR/c2.out" "$TMP_DIR/c2.err")
if [[ "$RC" -ne 0 ]]; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case2_unexpected_exit:%s\n' "$RC"
  cat "$TMP_DIR/c2.out"
  cat "$TMP_DIR/c2.err" >&2
  exit 1
fi
if ! grep -Fq 'SKIP_BACKLOG_CAP:PASS:#409:grandfathered_open_over_cap:count=13:cap=12:allowlist=13' "$TMP_DIR/c2.out"; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case2_missing_grandfathered_pass\n'
  cat "$TMP_DIR/c2.out" >&2
  exit 1
fi

# Case 3: count grows above grandfathered ceiling => FAIL.
MARKERS3="$SANDBOX/tests/m7_toolchain/markers_case3.json"
write_markers "$MARKERS3" 14
RC=$(run_cap_check "$MARKERS3" "$ALLOW2" "$TMP_DIR/c3.out" "$TMP_DIR/c3.err")
if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case3_unexpected_exit:%s\n' "$RC"
  cat "$TMP_DIR/c3.out"
  cat "$TMP_DIR/c3.err" >&2
  exit 1
fi
if ! grep -Fq 'SKIP_BACKLOG_CAP:FAIL:#409:grandfathered_ceiling_exceeded:count=14:allowlist=13' "$TMP_DIR/c3.err"; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case3_missing_ceiling_marker\n'
  cat "$TMP_DIR/c3.err" >&2
  exit 1
fi

# Case 4: once count drops to cap, allowlist entry must be removed => FAIL.
MARKERS4="$SANDBOX/tests/m7_toolchain/markers_case4.json"
write_markers "$MARKERS4" 12
RC=$(run_cap_check "$MARKERS4" "$ALLOW2" "$TMP_DIR/c4.out" "$TMP_DIR/c4.err")
if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case4_unexpected_exit:%s\n' "$RC"
  cat "$TMP_DIR/c4.out"
  cat "$TMP_DIR/c4.err" >&2
  exit 1
fi
if ! grep -Fq 'SKIP_BACKLOG_CAP:FAIL:#409:stale_allowlist_entry:count=12:cap=12' "$TMP_DIR/c4.err"; then
  printf 'TEST:FAIL:skip_backlog_cap_fixture:case4_missing_stale_allowlist_marker\n'
  cat "$TMP_DIR/c4.err" >&2
  exit 1
fi

printf 'TEST:PASS:skip_backlog_cap_fixture\n'
exit 0
