#!/usr/bin/env bash
# Wrapper for tools/validate_manifestgen_default_arena.py (issue #595).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "MANIFESTGEN_DEFAULT_ARENA:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_manifestgen_default_arena.py" --root "$ROOT_DIR" "$@"
