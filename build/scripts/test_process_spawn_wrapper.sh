#!/usr/bin/env bash
# @file test_process_spawn_wrapper.sh
# @brief M7-TOOLCHAIN-003 slice 2 (#422) — host-side smoke for
#        `os_process_spawn`.
#
# Mirrors `test_process_exit_wrapper.sh`: compile the wrapper test
# together with the user-runtime stubs and run it. Locks in the
# wrapper's exported symbol, signature, no-bridge fall-through, and
# the reserved-flag refusal contract documented in
# `docs/abi/syscalls.md`.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/process_spawn_wrapper_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/process_spawn_wrapper_test"

"$OUT_DIR/process_spawn_wrapper_test"
