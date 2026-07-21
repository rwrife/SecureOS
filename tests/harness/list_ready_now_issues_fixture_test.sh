#!/usr/bin/env bash
# tests/harness/list_ready_now_issues_fixture_test.sh
#
# Fixture canary for tools/list_ready_now_issues.py (issue #626).
#
# Asserts that the ready-now index:
# - filters daily-review snapshots,
# - excludes open dependency/gating references,
# - keeps documentation and CI/stamp-like slices with no open deps,
# - emits the stable JSON schema required by #626.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:list_ready_now_issues_fixture\n'

cat > "$TMP_DIR/open-issues.json" <<'EOF'
[
  {"number": 408, "title": "M7 gate #408", "body": "core gating issue", "labels": [{"name": "enhancement"}]},
  {"number": 410, "title": "M7 gate #410", "body": "core gating issue", "labels": [{"name": "enhancement"}]},
  {"number": 500, "title": "Daily review: 2026-07-20", "body": "snapshot", "labels": [{"name": "documentation"}]},
  {"number": 501, "title": "docs(contrib): improve triage doc", "body": "Independent docs slice.", "labels": [{"name": "documentation"}]},
  {"number": 502, "title": "ci(abi): bump manifest Last-verified stamp", "body": "Standalone stamp bump.", "labels": [{"name": "enhancement"}]},
  {"number": 503, "title": "docs(m7): blocked fixture", "body": "This work is gated by #410 and depends on #408.", "labels": [{"name": "documentation"}]},
  {"number": 504, "title": "docs(api): depends on open non-gate", "body": "depends on #505", "labels": [{"name": "documentation"}]},
  {"number": 505, "title": "follow-up issue", "body": "open dependency target", "labels": [{"name": "enhancement"}]},
  {"number": 506, "title": "refactor: unrelated code task", "body": "No dependency references.", "labels": [{"name": "enhancement"}]},
  {"number": 700, "title": "PR payload", "body": "ignore me", "pull_request": {"url": "https://api.github.com/repos/rwrife/SecureOS/pulls/700"}, "labels": [{"name": "documentation"}]}
]
EOF

OUT_JSON="$TMP_DIR/ready-now.json"
OUT_STDOUT="$TMP_DIR/stdout.log"
OUT_STDERR="$TMP_DIR/stderr.log"

set +e
python3 "$ROOT_DIR/tools/list_ready_now_issues.py" \
  --issues-file "$TMP_DIR/open-issues.json" \
  --generated-at "2099-01-01T00:00:00Z" \
  --output-json "$OUT_JSON" \
  --limit 10 \
  > "$OUT_STDOUT" 2> "$OUT_STDERR"
RC=$?
set -e

if [[ "$RC" -ne 0 ]]; then
  printf 'TEST:FAIL:list_ready_now_issues_fixture:unexpected_exit:%d\n' "$RC"
  cat "$OUT_STDOUT"
  cat "$OUT_STDERR" >&2
  exit 1
fi

if [[ ! -f "$OUT_JSON" ]]; then
  printf 'TEST:FAIL:list_ready_now_issues_fixture:missing_json_output\n'
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
        print(f"TEST:FAIL:list_ready_now_issues_fixture:{msg}")
        sys.exit(1)

data = json.loads(json_path.read_text(encoding="utf-8"))
require(data.get("generated_at") == "2099-01-01T00:00:00Z", "generated_at_mismatch")

open_gates = data.get("gating_issues_open")
require(open_gates == ["#408", "#410"], "gating_issues_open_mismatch")

candidates = data.get("candidates")
require(isinstance(candidates, list), "candidates_missing")
nums = [row.get("number") for row in candidates]

# Included expected ready-now slices.
require(501 in nums, "missing_docs_candidate")
require(502 in nums, "missing_ci_candidate")

# Excluded blocked/dependent/daily-review/non-doc-ci cases.
require(500 not in nums, "daily_review_not_filtered")
require(503 not in nums, "gated_issue_not_filtered")
require(504 not in nums, "depends_on_open_not_filtered")
require(506 not in nums, "non_doc_ci_not_filtered")

for row in candidates:
    require(set(row.keys()) == {"number", "title", "labels", "reason_ready"}, "candidate_schema_mismatch")
    require(isinstance(row["labels"], list), "candidate_labels_not_list")
    require(isinstance(row["reason_ready"], str) and row["reason_ready"], "candidate_reason_missing")

stdout = stdout_path.read_text(encoding="utf-8")
require("# Ready-now issue candidates" in stdout, "human_summary_missing")
require("## JSON" in stdout, "json_block_missing")

print("TEST:PASS:list_ready_now_issues_fixture")
PY

if [[ "$?" -ne 0 ]]; then
  exit 1
fi

exit 0
