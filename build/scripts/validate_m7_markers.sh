#!/usr/bin/env bash
# build/scripts/validate_m7_markers.sh
#
# Thin wrapper around tools/validate_m7_markers.py so the M7-markers
# drift gate runs through the same build/scripts/*.sh entrypoint as
# every other validator (issue #494; #234 / #297 / #351 pattern).
#
# Asserts that tests/m7_toolchain/markers.json (scaffolded by #423,
# umbrella #403) stays consistent with:
#   - the `toolchain_*` case arms in build/scripts/test.sh
#   - the `TEST_TARGETS` array in build/scripts/validate_bundle.sh
#   - the per-marker TEST:PASS / TEST:SKIP literals emitted by each
#     tests/m7_toolchain/<marker>.sh stub
#   - each `gatingIssue` still being OPEN on rwrife/SecureOS (when
#     `gh` is reachable; --allow-offline skips that arm with a
#     deterministic SKIP marker).
#
# Exit codes are passed through from the Python implementation:
#   0  every check passed
#   1  one or more M7_MARKER:FAIL markers emitted
#   2  environment / usage error (missing input, malformed JSON, ...)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "M7_MARKER:FAIL:python3_not_found" >&2
  exit 2
fi

# Default to --allow-offline when neither `gh` is on PATH nor the
# caller has explicitly opted into a live lookup. The bundle gate
# runs on hosts that often lack network, and the offline arm still
# pins every other drift class. Callers can force-online by passing
# --no-allow-offline or any other flag set explicitly.
ARGS=("$@")
if [[ ${#ARGS[@]} -eq 0 ]]; then
  if ! command -v gh >/dev/null 2>&1; then
    ARGS+=(--allow-offline)
  fi
fi

exec "$PY" "$ROOT_DIR/tools/validate_m7_markers.py" \
  --root "$ROOT_DIR" "${ARGS[@]}"
