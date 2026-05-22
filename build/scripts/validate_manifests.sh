#!/usr/bin/env bash
# build/scripts/validate_manifests.sh
#
# Issue #195: thin wrapper around tools/validate_manifests.py so the
# manifest schema check runs through the same `build/scripts/*.sh`
# entrypoint convention as every other validator. Mirror script:
# build/scripts/validate_manifests.ps1 (AGENTS.md cross-platform parity
# rule, #156).
#
# Exit codes:
#   0 — all manifests validated against manifests/schema/v0.json
#   1 — one or more manifests failed schema validation
#   2 — environment / usage error (missing schema, missing jsonschema lib)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "MANIFEST_VALIDATE:ERROR:python3 not found on PATH" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_manifests.py" "$@"
