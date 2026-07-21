#!/usr/bin/env bash
# build/scripts/validate_m7_marker_harnesses.sh
#
# Thin wrapper for tools/validate_m7_marker_harnesses.py (issue #604).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "M7_HARNESS:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_m7_marker_harnesses.py" --root "$ROOT_DIR" "$@"
