#!/usr/bin/env bash
# @file test_mem_brk_wrapper.sh
# @brief M7-TOOLCHAIN-001 slice 2 (#421) — host-side smoke for
#        `os_mem_brk`.
#
# Mirrors `test_process_exit_wrapper.sh`: compile the wrapper test
# together with the user-runtime stubs and run it. The mirror
# PowerShell script is `build/scripts/test_mem_brk_wrapper.ps1`
# (#156 parity rule).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/mem_brk_wrapper_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/mem_brk_wrapper_test"

"$OUT_DIR/mem_brk_wrapper_test"
