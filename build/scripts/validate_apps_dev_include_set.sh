#!/usr/bin/env bash
# Wrapper for tools/validate_apps_dev_include_set.py (issue #615).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "APPS_DEV_INCLUDE_SET:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_apps_dev_include_set.py" --root "$ROOT_DIR" "$@"
