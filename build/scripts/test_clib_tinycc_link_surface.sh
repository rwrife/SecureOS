#!/usr/bin/env bash
# build/scripts/test_clib_tinycc_link_surface.sh
#
# Build + run the TinyCC runtime-compat link-surface host gate
# (issue #539 / M7-TOOLCHAIN-005).
#
# Covers:
#   - Symbol presence/link resolution for the remaining #539 surface:
#       realloc, free, sprintf, exit, time, localtime,
#       getcwd, getenv, realpath, dlopen, dlsym.
#   - Deterministic runtime behavior for the compatibility stubs.
#
# Compiled with -fno-builtin so this gate exercises SecureOS clib symbols,
# not host-libc compiler builtins.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/user/libs/clib/src/malloc.c" \
  "$ROOT_DIR/user/libs/clib/src/runtime_compat.c" \
  "$ROOT_DIR/user/libs/clib/src/stdio.c" \
  "$ROOT_DIR/user/libs/clib/src/stdlib.c" \
  "$ROOT_DIR/tests/clib_tinycc_link_surface_test.c" \
  -I"$ROOT_DIR/user/libs/clib/include" \
  -I"$ROOT_DIR/user/include" \
  -o "$OUT_DIR/clib_tinycc_link_surface_test"

LOG_PATH="$OUT_DIR/clib_tinycc_link_surface_test.log"
"$OUT_DIR/clib_tinycc_link_surface_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_tinycc_link_surface:symbol_set_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:allocator_init" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:realloc_allocates" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:realloc_grows" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:realloc_preserves_prefix" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:free_forwarder_invoked" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:getcwd_fixed_value" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:getcwd_small_buffer_erange" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:getenv_null_stub" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:time_fixed_epoch" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:localtime_null_einval" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:localtime_fixed_breakdown" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:realpath_passthrough_pointer" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:realpath_copy" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:dlopen_enotsup" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:dlsym_enotsup" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface:sprintf_works" "$LOG_PATH"
grep -q "TEST:PASS:clib_tinycc_link_surface$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
