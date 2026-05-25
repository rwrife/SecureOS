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

exec "$PY" "$ROOT_DIR/tools/validate_abi_stamps.py" --root "$ROOT_DIR" "$@"
