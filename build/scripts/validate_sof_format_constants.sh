#!/usr/bin/env bash
# Wrapper for tools/validate_sof_format_constants.py (issue #547).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "SOF_FORMAT_CONSTANTS:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_sof_format_constants.py" --root "$ROOT_DIR" "$@"
