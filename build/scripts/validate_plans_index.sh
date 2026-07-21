#!/usr/bin/env bash
# build/scripts/validate_plans_index.sh
#
# Thin wrapper around tools/validate_plans_index.py so plans index validation
# follows the standard build/scripts entrypoint pattern.
# Mirror script: build/scripts/validate_plans_index.ps1.
#
# Exit codes:
#   0 — all plans are indexed exactly once
#   1 — one or more plans are missing / duplicated / stale-indexed
#   2 — usage / environment error

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "PLANS_INDEX:ERROR:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_plans_index.py" --root "$ROOT_DIR" "$@"
