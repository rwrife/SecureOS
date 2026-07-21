#!/usr/bin/env bash
# tests/harness/summarize_m7_backlog_fixture_test.sh
#
# Fixture canary for tools/summarize_m7_backlog.py (issue #627).
#
# Builds a synthetic markers.json + open-issues fixture and asserts:
# - only still-open gating issues from the target set are emitted,
# - markers_pinned / issues_gated counts are correct,
# - harness_paths and issue_numbers lists are deterministic,
# - stdout markdown summary fits on one screen (<= 40 lines).

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:summarize_m7_backlog_fixture\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tests/m7_toolchain" "$SANDBOX/out"

cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {"name": "toolchain_alpha", "gatingIssue": 410, "reason": "awaiting_410", "description": "alpha"},
    {"name": "toolchain_beta", "gatingIssue": 410, "reason": "awaiting_410", "description": "beta"},
    {"name": "toolchain_gamma", "gatingIssue": 406, "reason": "awaiting_406", "description": "gamma"},
    {"name": "toolchain_done", "gatingIssue": 410, "reason": "landed", "description": "not skip-pinned"},
    {"name": "toolchain_closed_gate", "gatingIssue": 585, "reason": "awaiting_585", "description": "closed gate fixture"}
  ]
}
EOF

cat > "$SANDBOX/open-issues.json" <<'EOF'
[
  {"number": 410, "body": "canonical gate issue"},
  {"number": 406, "body": "canonical gate issue"},
  {"number": 777, "body": "Follow-up is gated by #410 and blocks #406 before landing."},
  {"number": 778, "body": "Tracking pre-#410 checklist."},
  {"number": 779, "body": "Unrelated issue text."},
  {"number": 800, "body": "This one is a PR payload and must be ignored.", "pull_request": {"url": "https://api.github.com/repos/rwrife/SecureOS/pulls/800"}}
]
EOF

OUT_JSON="$SANDBOX/out/summary-2099-01-01.json"
OUT_STDOUT="$TMP_DIR/stdout.log"
OUT_STDERR="$TMP_DIR/stderr.log"

set +e
python3 "$ROOT_DIR/tools/summarize_m7_backlog.py" \
  --root "$SANDBOX" \
  --markers "$SANDBOX/tests/m7_toolchain/markers.json" \
  --issues-file "$SANDBOX/open-issues.json" \
  --output-dir "$SANDBOX/out" \
  --date "2099-01-01" \
  --generated-at "2099-01-01T00:00:00Z" \
  > "$OUT_STDOUT" 2> "$OUT_STDERR"
RC=$?
set -e

if [[ "$RC" -ne 0 ]]; then
  printf 'TEST:FAIL:summarize_m7_backlog_fixture:unexpected_exit:%d\n' "$RC"
  cat "$OUT_STDOUT"
  cat "$OUT_STDERR" >&2
  exit 1
fi

if [[ ! -f "$OUT_JSON" ]]; then
  printf 'TEST:FAIL:summarize_m7_backlog_fixture:missing_json_output\n'
  exit 1
fi

python3 - <<'PY' "$OUT_JSON" "$OUT_STDOUT"
import json
import sys
from pathlib import Path

json_path = Path(sys.argv[1])
stdout_path = Path(sys.argv[2])

def require(cond, msg):
    if not cond:
        print(f"TEST:FAIL:summarize_m7_backlog_fixture:{msg}")
        sys.exit(1)

data = json.loads(json_path.read_text(encoding="utf-8"))
per = data.get("per_gating_issue")
require(isinstance(per, dict), "missing_per_gating_issue")
require(data.get("generated_at") == "2099-01-01T00:00:00Z", "generated_at_mismatch")

require(set(per.keys()) == {"#410", "#406"}, "unexpected_gate_keys")

require(per["#410"]["markers_pinned"] == 2, "gate_410_markers_mismatch")
require(per["#410"]["issues_gated"] == 2, "gate_410_issues_gated_mismatch")
require(per["#410"]["harness_paths"] == [
    "tests/m7_toolchain/toolchain_alpha.sh",
    "tests/m7_toolchain/toolchain_beta.sh",
], "gate_410_harness_paths_mismatch")
require(per["#410"]["issue_numbers"] == [777, 778], "gate_410_issue_numbers_mismatch")

require(per["#406"]["markers_pinned"] == 1, "gate_406_markers_mismatch")
require(per["#406"]["issues_gated"] == 1, "gate_406_issues_gated_mismatch")
require(per["#406"]["harness_paths"] == [
    "tests/m7_toolchain/toolchain_gamma.sh",
], "gate_406_harness_paths_mismatch")
require(per["#406"]["issue_numbers"] == [777], "gate_406_issue_numbers_mismatch")

stdout_lines = stdout_path.read_text(encoding="utf-8").splitlines()
require(len(stdout_lines) <= 40, "stdout_summary_too_long")
require(any("| gating issue | markers pinned | open issues gated | harness paths |" in ln for ln in stdout_lines), "stdout_table_missing")

print("TEST:PASS:summarize_m7_backlog_fixture")
PY

if [[ "$?" -ne 0 ]]; then
  exit 1
fi

exit 0
