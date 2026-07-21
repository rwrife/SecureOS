#!/usr/bin/env bash
# Wrapper for tools/validate_dev_building_txt.py (issue #618).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "DEV_BUILDING_TXT:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_dev_building_txt.py" --root "$ROOT_DIR" "$@"
