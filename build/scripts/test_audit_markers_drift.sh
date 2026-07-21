#!/usr/bin/env bash
# build/scripts/test_audit_markers_drift.sh
#
# Issue #591: markdown<->json drift gate for docs/abi audit markers.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "AUDIT_MARKERS:FAIL:python3_not_found" >&2
  exit 2
fi

EXTRA_ARGS=()
if [[ "${AUDIT_MARKERS_WITH_GH:-0}" == "1" ]]; then
  EXTRA_ARGS+=(--with-gh)
fi

exec "$PY" "$ROOT_DIR/tools/validate_audit_markers.py" --root "$ROOT_DIR" "${EXTRA_ARGS[@]}"
