#!/usr/bin/env bash
# Wrapper for tools/validate_disk_image_apps_dev_sha.py (issue #606).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "APPS_DEV_SHA:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_disk_image_apps_dev_sha.py" --root "$ROOT_DIR" "$@"
