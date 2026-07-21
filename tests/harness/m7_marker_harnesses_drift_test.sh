#!/usr/bin/env bash
# tests/harness/m7_marker_harnesses_drift_test.sh
#
# Issue #604 negative canary for tools/validate_m7_marker_harnesses.py.
# Proves markers.json rows without a sibling harness path fail with
# deterministic diagnostics naming expected file paths.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:m7_marker_harnesses_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tools" "$SANDBOX/tests/m7_toolchain"

cp "$ROOT_DIR/tools/validate_m7_marker_harnesses.py" \
  "$SANDBOX/tools/validate_m7_marker_harnesses.py" \
  || { printf 'TEST:FAIL:m7_marker_harnesses_drift_canary:tool_copy_failed\n'; exit 1; }

cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "name": "toolchain_present_harness",
      "gatingIssue": 409,
      "reason": "awaiting_409",
      "description": "fixture present"
    },
    {
      "name": "toolchain_missing_harness",
      "gatingIssue": 410,
      "reason": "awaiting_410",
      "description": "fixture missing"
    }
  ]
}
EOF

cat > "$SANDBOX/tests/m7_toolchain/toolchain_present_harness.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'TEST:PASS:toolchain_present_harness\n'
EOF

cat > "$SANDBOX/tests/m7_toolchain/marker_harness_allowlist.json" <<'EOF'
{
  "schemaVersion": 1,
  "entries": []
}
EOF

PY="${PYTHON:-python3}"
OUT="$TMP_DIR/stdout"
ERR="$TMP_DIR/stderr"

set +e
"$PY" "$SANDBOX/tools/validate_m7_marker_harnesses.py" --root "$SANDBOX" >"$OUT" 2>"$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_marker_harnesses_drift_canary:unexpected_exit_code:%d\n' "$RC"
  cat "$OUT" >&2
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'M7_HARNESS:FAIL:toolchain_missing_harness:missing_harness:expected_one_of:tests/m7_toolchain/toolchain_missing_harness.sh|tests/m7_toolchain/toolchain_missing_harness.c' "$ERR"; then
  printf 'TEST:FAIL:m7_marker_harnesses_drift_canary:missing_expected_diagnostic\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'M7_HARNESS:FAIL:summary:1_failures' "$ERR"; then
  printf 'TEST:FAIL:m7_marker_harnesses_drift_canary:missing_summary_fail\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'M7_HARNESS:PASS:toolchain_present_harness:harness_present:tests/m7_toolchain/toolchain_present_harness.sh' "$OUT"; then
  printf 'TEST:FAIL:m7_marker_harnesses_drift_canary:missing_present_harness_pass\n'
  cat "$OUT" >&2
  exit 1
fi

printf 'TEST:PASS:m7_marker_harnesses_drift_canary\n'
exit 0
