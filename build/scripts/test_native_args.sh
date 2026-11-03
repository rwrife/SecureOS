#!/usr/bin/env bash
# Host regression gate for native launcher raw-argument truncation (#767).
# Called by test.sh and validate_bundle.sh.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$ROOT_DIR/artifacts/tests"
cc -std=c11 -ffunction-sections -fdata-sections \
  -I "$ROOT_DIR" -I "$ROOT_DIR/kernel" \
  "$ROOT_DIR/tests/native_args_test.c" -Wl,--gc-sections \
  -o "$ROOT_DIR/artifacts/tests/native_args_test"
"$ROOT_DIR/artifacts/tests/native_args_test"
printf 'TEST:PASS:native_args\n'
