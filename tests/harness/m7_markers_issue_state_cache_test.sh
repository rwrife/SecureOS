#!/usr/bin/env bash
# tests/harness/m7_markers_issue_state_cache_test.sh
#
# Canary for issue-state resolution in tools/validate_m7_markers.py (issue #590).
# Proves offline-cache mode fails when a marker still awaits a CLOSED issue,
# and passes once the reason is retargeted away from that closed issue.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:m7_markers_issue_state_cache_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tools" \
         "$SANDBOX/tests/m7_toolchain" \
         "$SANDBOX/build/scripts"

cp "$ROOT_DIR/tools/validate_m7_markers.py" \
   "$SANDBOX/tools/validate_m7_markers.py" \
  || { printf 'TEST:FAIL:m7_markers_issue_state_cache_canary:tool_copy_failed\n'; exit 1; }

cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "umbrella": 403,
  "markers": [
    {
      "name": "toolchain_demo_marker",
      "gatingIssue": 123,
      "reason": "awaiting_123",
      "description": "fixture marker"
    }
  ]
}
EOF

cat > "$SANDBOX/tests/m7_toolchain/toolchain_demo_marker.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'TEST:SKIP:toolchain_demo_marker:awaiting_123\n'
printf 'TEST:PASS:toolchain_demo_marker\n'
EOF
chmod +x "$SANDBOX/tests/m7_toolchain/toolchain_demo_marker.sh"

cat > "$SANDBOX/build/scripts/test.sh" <<'EOF'
#!/usr/bin/env bash
TEST_NAME="$1"
case "$TEST_NAME" in
  toolchain_demo_marker)
    bash "tests/m7_toolchain/${TEST_NAME}.sh"
    ;;
esac
EOF

cat > "$SANDBOX/build/scripts/validate_bundle.sh" <<'EOF'
#!/usr/bin/env bash
TEST_TARGETS=(
  toolchain_demo_marker
)
EOF

cat > "$SANDBOX/tests/m7_toolchain/issue_state.cache.json" <<'EOF'
{
  "schemaVersion": 1,
  "issues": {
    "123": { "state": "CLOSED", "replacedBy": 456 }
  }
}
EOF

PY="${PYTHON:-python3}"
OUT1="$TMP_DIR/out1"
ERR1="$TMP_DIR/err1"

set +e
"$PY" "$SANDBOX/tools/validate_m7_markers.py" \
  --root "$SANDBOX" \
  --offline-cache-only \
  --issue-state-cache tests/m7_toolchain/issue_state.cache.json \
  > "$OUT1" 2> "$ERR1"
RC1=$?
set -e

if [[ "$RC1" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_markers_issue_state_cache_canary:expected_rc1_got_%d\n' "$RC1"
  cat "$OUT1" >&2
  cat "$ERR1" >&2
  exit 1
fi

if ! grep -Fq 'M7_MARKER:FAIL:toolchain_demo_marker:gating_issue_closed_but_reason_still_awaiting:123:cache:hit:replaced_by:456' "$ERR1"; then
  printf 'TEST:FAIL:m7_markers_issue_state_cache_canary:missing_closed_awaiting_fail_marker\n'
  cat "$ERR1" >&2
  exit 1
fi

# Retarget reason off the closed issue id; validator should allow it.
python3 - <<'PY' "$SANDBOX/tests/m7_toolchain/markers.json"
import json, pathlib, sys
p = pathlib.Path(sys.argv[1])
data = json.loads(p.read_text())
data["markers"][0]["reason"] = "awaiting_456"
p.write_text(json.dumps(data, indent=2) + "\n")
PY

OUT2="$TMP_DIR/out2"
ERR2="$TMP_DIR/err2"

set +e
"$PY" "$SANDBOX/tools/validate_m7_markers.py" \
  --root "$SANDBOX" \
  --offline-cache-only \
  --issue-state-cache tests/m7_toolchain/issue_state.cache.json \
  > "$OUT2" 2> "$ERR2"
RC2=$?
set -e

if [[ "$RC2" -ne 0 ]]; then
  printf 'TEST:FAIL:m7_markers_issue_state_cache_canary:expected_rc0_after_retarget_got_%d\n' "$RC2"
  cat "$OUT2" >&2
  cat "$ERR2" >&2
  exit 1
fi

if ! grep -Fq 'M7_MARKER:PASS:toolchain_demo_marker:gating_issue_closed_reason_retargeted:123:cache:hit' "$OUT2"; then
  printf 'TEST:FAIL:m7_markers_issue_state_cache_canary:missing_retarget_pass_marker\n'
  cat "$OUT2" >&2
  exit 1
fi

printf 'TEST:PASS:m7_markers_issue_state_cache_canary\n'
exit 0
