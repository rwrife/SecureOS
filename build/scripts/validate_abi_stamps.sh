#!/usr/bin/env bash
# build/scripts/validate_abi_stamps.sh
#
# Issue #297: thin wrapper around tools/validate_abi_stamps.py so the
# ABI-stamp-freshness validator runs through the same build/scripts/*.sh
# entrypoint as every other validator (issue #234 pattern). Mirror script:
# build/scripts/validate_abi_stamps.ps1 (#156 parity rule).
#
# Exit codes are passed through from the Python implementation:
#   0  every in-scope docs/abi/*.md stamp is at least as new as its
#      most recent content-changing commit
#   1  one or more ABI_STAMP:FAIL markers emitted (stale stamps)
#   2  environment / usage error (missing dir, not a git checkout)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "ABI_STAMP:FAIL:python3_not_found" >&2
  exit 2
fi

# Issue #470: 'no_stamp_line' SKIP is promoted to FAIL so a new ABI doc cannot
# silently bypass the freshness gate. The four pre-existing SKIP files
# (#463 / #467 / #468 plus the two *-contract.md docs) all carry stamps on
# main now, so the wrapper default flipped to strict. Set STRICT_STAMPS=0 to
# fall back to the legacy SKIP-tolerant mode (only intended as a short-term
# escape hatch when intentionally landing an unstamped doc behind --exempt
# in the same change).
STRICT_ARGS=()
if [ "${STRICT_STAMPS:-1}" != "0" ]; then
  STRICT_ARGS+=(--strict-no-skip)
fi

exec "$PY" "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$ROOT_DIR" \
  ${STRICT_ARGS[@]+"${STRICT_ARGS[@]}"} "$@"
