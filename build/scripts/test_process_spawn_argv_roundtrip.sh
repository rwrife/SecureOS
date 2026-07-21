#!/usr/bin/env bash
# @file test_process_spawn_argv_roundtrip.sh
# @brief Issue #546 — host-side argv + exit-status contract pin for
#        `os_process_spawn`.
#
# Compiles tests/process_spawn_argv_roundtrip_test.c with the user runtime
# stubs and runs it. The test maps a synthetic native bridge page so the
# wrapper can be driven dynamically on host (including argv join behavior and
# out_exit_status propagation).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/process_spawn_argv_roundtrip_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/process_spawn_argv_roundtrip_test"

"$OUT_DIR/process_spawn_argv_roundtrip_test"
