#!/usr/bin/env bash
# build/scripts/validate_sosh_capability_contract.sh
#
# Thin wrapper around tools/validate_sosh_capability_contract.py so the
# sosh-contract drift validator runs through the same build/scripts/*.sh
# entrypoint as every other validator (issue #234 pattern, #297 mirror).
#
# Asserts every CAP_* cited in §4 of docs/abi/sosh-capability-contract.md
# round-trips against docs/abi/capability-registry.json. This protects the
# in-flight enforcement slice (PR #358 + follow-ups) from quietly gating
# against a capability id that does not exist in the registry, and from
# the inverse drift mode where the registry renames a cap but the
# contract still cites the old marker spelling.
#
# Exit codes are passed through from the Python implementation:
#   0  every §4 row passes both the existence and marker-name checks
#   1  one or more SOSH_CONTRACT:FAIL markers emitted
#   2  environment / usage error (missing input file, malformed JSON,
#      contract doc lacks a §4 table)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "SOSH_CONTRACT:FAIL:python3_not_found" >&2
  exit 2
fi

exec "$PY" "$ROOT_DIR/tools/validate_sosh_capability_contract.py" --root "$ROOT_DIR" "$@"
