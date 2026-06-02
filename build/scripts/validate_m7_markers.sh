#!/usr/bin/env bash
# build/scripts/validate_m7_markers.sh
#
# Thin wrapper around tools/validate_m7_markers.py so the M7-TOOLCHAIN
# acceptance-suite drift validator runs through the same
# build/scripts/*.sh entrypoint as every other validator
# (issue #494, mirrors #234 / #297 / #351).
#
# Exit codes are passed through from the Python implementation:
#   0  markers.json consistent with test.sh + validate_bundle.sh +
#      tests/m7_toolchain/<marker>.sh stubs (gating-issue state OK)
#   1  one or more M7_MARKER:FAIL markers emitted
#   2  environment / usage error (missing input file, malformed JSON)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "M7_MARKER:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_m7_markers.py" --root "$ROOT_DIR" "$@"
