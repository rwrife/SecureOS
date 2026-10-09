#!/usr/bin/env bash
# Host regression gate for cc argument overflow (DEMO-03 #767).
# Called by test.sh and validate_bundle.sh; compiles the real driver parser.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
mkdir -p "$OUT_DIR"
cc -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I "$ROOT_DIR/user/include" \
  -I "$ROOT_DIR/user/libs/clib/include/clib" \
  -I "$ROOT_DIR/user/libs/sofpack/include" \
  -I "$ROOT_DIR/user/libs/manifestgen/include" \
  -I "$ROOT_DIR/vendor/tinycc/tinycc" \
  "$ROOT_DIR/tests/cc_args_test.c" -Wl,--gc-sections -o "$OUT_DIR/cc_args_test"
"$OUT_DIR/cc_args_test"
printf 'TEST:PASS:cc_args\n'
