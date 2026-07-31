#!/usr/bin/env bash
# tests/harness/audit_markers_drift_test.sh
#
# Issue #591 negative canary: prove validator fails when markdown/json
# marker-prefix sets diverge.
#
# Issue #554 extension: also prove validator fails when either launch
# marker shape drops `owner_kind=<owner_kind>`.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:audit_markers_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tools" "$SANDBOX/docs/abi"

cp "$ROOT_DIR/tools/validate_audit_markers.py" \
  "$SANDBOX/tools/validate_audit_markers.py" \
  || { printf 'TEST:FAIL:audit_markers_drift_canary:validator_copy_failed\n'; exit 1; }

PY="${PYTHON:-python3}"
OUT="$TMP_DIR/stdout"
ERR="$TMP_DIR/stderr"
OUT2="$TMP_DIR/stdout2"
ERR2="$TMP_DIR/stderr2"

# ---------------------------------------------------------------------------
# Case 1: markdown/json prefix drift (launch.denied missing from JSON)
# ---------------------------------------------------------------------------
cat > "$SANDBOX/docs/abi/audit-markers.md" <<'EOF'
# Audit Marker Registry (fixture)

## 3. Marker catalog

| marker_prefix | family | emitter | consumer / test surface | authoritative doc | gating issue |
| --- | --- | --- | --- | --- | --- |
| `launch.granted` / `launch.denied` | Launcher execution-decision audit | fixture | fixture | [`audit-markers.md`](audit-markers.md) | [#554](https://github.com/rwrife/SecureOS/issues/554) |

## 4. End

Last verified against commit: fixture
EOF

cat > "$SANDBOX/docs/abi/audit-markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "prefix": "launch.granted",
      "family": "launcher-exec-decision",
      "shape": "launch.granted:owner_kind=<owner_kind>",
      "emitter": "fixture",
      "authoritative_doc": "docs/abi/audit-markers.md",
      "consumer_tests": ["tests/fixture"],
      "gating_issue": 554
    }
  ]
}
EOF

set +e
"$PY" "$SANDBOX/tools/validate_audit_markers.py" --root "$SANDBOX" >"$OUT" 2>"$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:audit_markers_drift_canary:unexpected_exit_code:%d\n' "$RC"
  cat "$OUT" >&2
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'AUDIT_MARKERS:FAIL:missing_in_json:launch.denied' "$ERR"; then
  printf 'TEST:FAIL:audit_markers_drift_canary:missing_expected_missing_in_json\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'AUDIT_MARKERS:FAIL:summary:1_failures' "$ERR"; then
  printf 'TEST:FAIL:audit_markers_drift_canary:missing_summary_fail\n'
  cat "$ERR" >&2
  exit 1
fi

if ! grep -Fq 'AUDIT_MARKERS:SKIP:gating_issue_check_disabled:use_--with-gh' "$OUT"; then
  printf 'TEST:FAIL:audit_markers_drift_canary:missing_expected_skip_marker\n'
  cat "$OUT" >&2
  exit 1
fi

# ---------------------------------------------------------------------------
# Case 2: launch.denied shape drift (owner_kind token removed)
# ---------------------------------------------------------------------------
cat > "$SANDBOX/docs/abi/audit-markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "prefix": "launch.granted",
      "family": "launcher-exec-decision",
      "shape": "launch.granted:owner_kind=<owner_kind>",
      "emitter": "fixture",
      "authoritative_doc": "docs/abi/audit-markers.md",
      "consumer_tests": ["tests/fixture"],
      "gating_issue": 554
    },
    {
      "prefix": "launch.denied",
      "family": "launcher-exec-decision",
      "shape": "launch.denied:subject=<sid>:reason=<reason>",
      "emitter": "fixture",
      "authoritative_doc": "docs/abi/audit-markers.md",
      "consumer_tests": ["tests/fixture"],
      "gating_issue": 554
    }
  ]
}
EOF

set +e
"$PY" "$SANDBOX/tools/validate_audit_markers.py" --root "$SANDBOX" >"$OUT2" 2>"$ERR2"
RC2=$?
set -e

if [[ "$RC2" -ne 1 ]]; then
  printf 'TEST:FAIL:audit_markers_drift_canary:owner_kind_case_unexpected_exit:%d\n' "$RC2"
  cat "$OUT2" >&2
  cat "$ERR2" >&2
  exit 1
fi

if ! grep -Fq 'AUDIT_MARKERS:FAIL:launch_owner_kind_shape:launch.denied' "$ERR2"; then
  printf 'TEST:FAIL:audit_markers_drift_canary:missing_owner_kind_shape_fail\n'
  cat "$ERR2" >&2
  exit 1
fi

if ! grep -Fq 'AUDIT_MARKERS:FAIL:summary:1_failures' "$ERR2"; then
  printf 'TEST:FAIL:audit_markers_drift_canary:owner_kind_missing_summary_fail\n'
  cat "$ERR2" >&2
  exit 1
fi

printf 'TEST:PASS:audit_markers_drift_canary\n'
exit 0
