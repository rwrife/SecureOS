#!/usr/bin/env bash
# tests/harness/skip_backlog_report_fixture_test.sh
#
# Negative/fixture canary for tools/report_skip_backlog.py (issue #631).
#
# Builds a synthetic SKIP-heavy tests tree and asserts the report generator
# counts source TEST:SKIP markers and per-gating-issue buckets correctly,
# including the required "unpinned" bucket.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:skip_backlog_report_fixture\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tests/m7_toolchain" "$SANDBOX/tests/other" "$SANDBOX/out"

cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {"name": "toolchain_alpha", "gatingIssue": 410, "reason": "awaiting_410", "description": "alpha"},
    {"name": "toolchain_beta", "gatingIssue": 410, "reason": "awaiting_410", "description": "beta"},
    {"name": "toolchain_gamma", "gatingIssue": 408, "reason": "awaiting_408", "description": "gamma"},
    {"name": "toolchain_unpinned", "reason": "manual_backlog", "description": "unpinned"}
  ]
}
EOF

cat > "$SANDBOX/tests/m7_toolchain/a.sh" <<'EOF'
#!/usr/bin/env bash
printf 'TEST:SKIP:toolchain_alpha:awaiting_410\n'
printf 'TEST:SKIP:toolchain_beta:awaiting_410\n'
EOF

cat > "$SANDBOX/tests/other/b.sh" <<'EOF'
#!/usr/bin/env bash
printf 'TEST:SKIP:toolchain_gamma:awaiting_408\n'
printf 'TEST:SKIP:toolchain_unpinned:manual_backlog\n'
printf 'TEST:SKIP:external_skip_without_registry:awaiting_585\n'
printf 'TEST:SKIP:another_unpinned:triage_later\n'
EOF

set +e
python3 "$ROOT_DIR/tools/report_skip_backlog.py" \
  --root "$SANDBOX" \
  --tests-dir "$SANDBOX/tests" \
  --markers "$SANDBOX/tests/m7_toolchain/markers.json" \
  --allow-offline \
  --report-week "2099-W01" \
  --generated-at "2099-01-01T00:00:00Z" \
  --output-dir "$SANDBOX/out" \
  > "$TMP_DIR/stdout.log" 2> "$TMP_DIR/stderr.log"
RC=$?
set -e

if [[ "$RC" -ne 0 ]]; then
  printf 'TEST:FAIL:skip_backlog_report_fixture:unexpected_exit:%d\n' "$RC"
  cat "$TMP_DIR/stdout.log"
  cat "$TMP_DIR/stderr.log" >&2
  exit 1
fi

JSON_PATH="$SANDBOX/out/2099-W01.json"
if [[ ! -f "$JSON_PATH" ]]; then
  printf 'TEST:FAIL:skip_backlog_report_fixture:missing_json_output\n'
  exit 1
fi

python3 - <<'PY' "$JSON_PATH"
import json
import sys

p = sys.argv[1]
data = json.load(open(p, "r", encoding="utf-8"))
body = data["body"]
counts = body["counts"]
per = body["per_gating_issue"]

def require(cond, msg):
    if not cond:
        print(f"TEST:FAIL:skip_backlog_report_fixture:{msg}")
        sys.exit(1)

require(counts["source_skip_markers_total"] == 6, "source_skip_total_mismatch")
require(counts["registry_markers_total"] == 4, "registry_total_mismatch")

require(per["#410"]["source_skip_markers"] == 2, "issue_410_source_mismatch")
require(per["#410"]["registry_markers"] == 2, "issue_410_registry_mismatch")

require(per["#408"]["source_skip_markers"] == 1, "issue_408_source_mismatch")
require(per["#408"]["registry_markers"] == 1, "issue_408_registry_mismatch")

require(per["#585"]["source_skip_markers"] == 1, "issue_585_source_mismatch")
require(per["#585"]["registry_markers"] == 0, "issue_585_registry_mismatch")

require(per["unpinned"]["source_skip_markers"] == 2, "unpinned_source_mismatch")
require(per["unpinned"]["registry_markers"] == 1, "unpinned_registry_mismatch")

closed = body["closed_gating_issues_referenced_by_skips"]
require(closed == [], "closed_issues_should_be_empty_offline")

print("TEST:PASS:skip_backlog_report_fixture")
PY

if [[ "$?" -ne 0 ]]; then
  exit 1
fi

exit 0
