#!/usr/bin/env bash
# build/scripts/test_clib_posix_fd.sh
#
# Build + run the freestanding POSIX-fd host unit test
# (issue #538 / M7-TOOLCHAIN-005).
#
# Covers:
#   - open: invalid args, denied-path errno mapping, and read-only gate.
#   - read/lseek: deterministic snapshot read + cursor movement semantics.
#   - close: valid and invalid-fd behavior.
#   - fd table saturation: EMFILE when fixed slot table is exhausted.
#   - unlink: truncate-to-empty shim (with errno mapping) over os_fs_write_file.
#   - symbol_set_pinned marker used by the bundle harness.
#
# Compiled with -fno-builtin so we validate libclib symbols (not host libc
# wrappers).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/user/libs/clib/src/posix_fd.c" \
  "$ROOT_DIR/tests/clib_posix_fd_test.c" \
  -I"$ROOT_DIR/user/libs/clib/include" \
  -I"$ROOT_DIR/user/include" \
  -o "$OUT_DIR/clib_posix_fd_test"

LOG_PATH="$OUT_DIR/clib_posix_fd_test.log"
"$OUT_DIR/clib_posix_fd_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_posix_fd:open_null_path" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_rejects_write_mode" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_maps_denied_to_eacces" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_alpha_success" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_prefix" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:lseek_set" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_middle" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:lseek_end_minus5" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_suffix" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_eof_returns_zero" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:close_valid_fd" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:fd_table_opened_some" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:fd_table_emfile" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:close_invalid_fd" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_invalid_fd" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:lseek_invalid_fd" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:unlink_missing_maps_enoent" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:unlink_denied_maps_eacces" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:unlink_truncate_success" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_after_unlink_success" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:read_after_unlink_is_eof" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:close_after_unlink_success" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:symbol_set_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
