#!/usr/bin/env bash
# build/scripts/validate_no_arch_macros_outside_arch_tree.sh
#
# Thin wrapper for the multi-arch portability drift gate (issue #623).
# The Python implementation scans kernel non-arch trees and fails when
# architecture preprocessor macros leak outside kernel/arch/**.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "ARCH_MACRO_VALIDATE:FAIL:python_not_found" >&2
  exit 2
fi

exec "$PY" \
  "$ROOT_DIR/tools/validate_no_arch_macros_outside_arch_tree.py" \
  --root "$ROOT_DIR" \
  --allowlist "$ROOT_DIR/build/scripts/.arch_macro_allowlist" \
  "$@"
