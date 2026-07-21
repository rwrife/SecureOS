#!/usr/bin/env bash
# build/scripts/test_plan_sections_drift.sh
#
# Thin wrapper around tools/validate_plan_sections.py (issue #593).
#
# Exit codes are passed through from the Python implementation:
#   0  validation passed (or gh checks cleanly SKIP offline)
#   1  unticketed, non-allowlisted slice token(s) found
#   2  usage / environment error

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "PLAN_SECTION:ERROR:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_plan_sections.py" --root "$ROOT_DIR" --with-gh "$@"
