#!/usr/bin/env bash
# build/scripts/validate_m7_markers_schema.sh
#
# Thin wrapper for tools/validate_m7_markers_schema.py (issue #611).
# Enforces schema/shape constraints for tests/m7_toolchain/markers.json rows.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "M7_SCHEMA:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_m7_markers_schema.py" --root "$ROOT_DIR" "$@"
