#!/usr/bin/env bash
# tests/harness/m7_markers_schema_drift_test.sh
#
# Negative canary for tools/validate_m7_markers_schema.py (issue #611).
# Proves row-shape violations are hard failures with deterministic markers.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:m7_markers_schema_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tools" "$SANDBOX/tests/m7_toolchain"

cp "$ROOT_DIR/tools/validate_m7_markers_schema.py" "$SANDBOX/tools/validate_m7_markers_schema.py" \
  || { printf 'TEST:FAIL:m7_markers_schema_drift_canary:tool_copy_failed\n'; exit 1; }

echo '#!/usr/bin/env bash' > "$SANDBOX/tests/m7_toolchain/toolchain_demo_marker.sh"

cat > "$SANDBOX/tests/m7_toolchain/markers.good.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "id": "toolchain_demo_marker",
      "name": "toolchain_demo_marker",
      "harnessPath": "tests/m7_toolchain/toolchain_demo_marker.sh",
      "gatingIssue": 409,
      "reason": "awaiting_409",
      "skipReason": "pre-409",
      "addedIn": "issue-611",
      "description": "demo"
    }
  ]
}
EOF

PY="${PYTHON:-python3}"
OUT_GOOD="$TMP_DIR/out.good"
ERR_GOOD="$TMP_DIR/err.good"

set +e
"$PY" "$SANDBOX/tools/validate_m7_markers_schema.py" \
  --root "$SANDBOX" --markers tests/m7_toolchain/markers.good.json \
  > "$OUT_GOOD" 2> "$ERR_GOOD"
RC_GOOD=$?
set -e

if [[ "$RC_GOOD" -ne 0 ]]; then
  printf 'TEST:FAIL:m7_markers_schema_drift_canary:expected_good_rc0_got_%d\n' "$RC_GOOD"
  cat "$OUT_GOOD" >&2
  cat "$ERR_GOOD" >&2
  exit 1
fi
if ! grep -Fq 'M7_SCHEMA:PASS:summary:1_rows_valid' "$OUT_GOOD"; then
  printf 'TEST:FAIL:m7_markers_schema_drift_canary:missing_good_summary\n'
  cat "$OUT_GOOD" >&2
  exit 1
fi

cat > "$SANDBOX/tests/m7_toolchain/markers.bad.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "name": "toolchain_demo_marker",
      "harnessPath": "tests/m7_toolchain/toolchain_demo_marker.sh",
      "gatingIssue": "409",
      "reason": "awaiting_409",
      "skipReason": "pre-999",
      "addedIn": "bad-format",
      "description": "demo",
      "unexpected": true
    }
  ]
}
EOF

OUT_BAD="$TMP_DIR/out.bad"
ERR_BAD="$TMP_DIR/err.bad"

set +e
"$PY" "$SANDBOX/tools/validate_m7_markers_schema.py" \
  --root "$SANDBOX" --markers tests/m7_toolchain/markers.bad.json \
  > "$OUT_BAD" 2> "$ERR_BAD"
RC_BAD=$?
set -e

if [[ "$RC_BAD" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_markers_schema_drift_canary:expected_bad_rc1_got_%d\n' "$RC_BAD"
  cat "$OUT_BAD" >&2
  cat "$ERR_BAD" >&2
  exit 1
fi

for needle in \
  'M7_SCHEMA:FAIL:row_0:missing_keys:id' \
  'M7_SCHEMA:FAIL:row_0:extra_keys:unexpected' \
  'M7_SCHEMA:FAIL:row_0:gatingIssue_not_integer:' \
  'M7_SCHEMA:FAIL:row_0:invalid_skipReason:' \
  'M7_SCHEMA:FAIL:summary:'; do
  if ! grep -Fq "$needle" "$ERR_BAD"; then
    printf 'TEST:FAIL:m7_markers_schema_drift_canary:missing_expected_marker:%s\n' "$needle"
    cat "$ERR_BAD" >&2
    exit 1
  fi
done

printf 'TEST:PASS:m7_markers_schema_drift_canary\n'
exit 0
