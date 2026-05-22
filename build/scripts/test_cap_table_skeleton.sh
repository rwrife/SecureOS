#!/usr/bin/env bash
# build/scripts/test_cap_table_skeleton.sh
#
# Compiles and runs tests/cap_table_skeleton_test.c against the M1 kernel
# capability handle table skeleton landed for issue #225 (M1-CAPTBL-001).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/cap/cap_handle.c" \
  "$ROOT_DIR/tests/cap_table_skeleton_test.c" \
  -o "$OUT_DIR/cap_table_skeleton_test"

"$OUT_DIR/cap_table_skeleton_test"
