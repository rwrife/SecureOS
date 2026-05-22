#!/usr/bin/env bash
# build/scripts/test_cap_handle_repr.sh
#
# Compiles and runs tests/cap_handle_repr_test.c against the M1
# capability handle representation layer landed for issue #233
# (M1-CAPTBL-002).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/tests/cap_handle_repr_test.c" \
  -o "$OUT_DIR/cap_handle_repr_test"

"$OUT_DIR/cap_handle_repr_test"
