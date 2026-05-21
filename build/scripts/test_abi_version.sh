#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/abi_version_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/abi_version_test"

"$OUT_DIR/abi_version_test"
