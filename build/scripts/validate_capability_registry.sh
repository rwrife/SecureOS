#!/usr/bin/env bash
# build/scripts/validate_capability_registry.sh
#
# Thin wrapper around tools/validate_capability_registry.py so the
# capability-registry validator runs through the same build/scripts/*.sh
# entrypoint as every other validator (issue #234). Mirror script:
# build/scripts/validate_capability_registry.ps1 (#156 parity rule).
#
# Exit codes are passed through from the Python implementation:
#   0  registry consistent with kernel/cap/capability.h + test.sh + plans/
#   1  one or more REGISTRY_VALIDATE:FAIL markers emitted
#   2  environment / usage error (missing input file, malformed JSON)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "REGISTRY_VALIDATE:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_capability_registry.py" --root "$ROOT_DIR" "$@"
