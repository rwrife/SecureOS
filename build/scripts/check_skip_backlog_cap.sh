#!/usr/bin/env bash
# build/scripts/check_skip_backlog_cap.sh
#
# Thin wrapper for tools/check_skip_backlog_cap.py (issue #641).
# Enforces per-open-gating-issue SKIP backlog cap from
# tests/m7_toolchain/markers.json with remove-only allowlist support.
#
# Exit codes (from Python implementation):
#   0  cap check passed
#   1  cap/policy violation
#   2  environment / usage error

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "SKIP_BACKLOG_CAP:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/check_skip_backlog_cap.py" --root "$ROOT_DIR" "$@"
