#!/usr/bin/env bash
# build/scripts/test_clib_posix_fd.sh
#
# Build + run the freestanding POSIX-fd host unit test
# (issue #538 / M7-TOOLCHAIN-005).
#
# Covers:
#   - open: invalid args, denied-path errno mapping, write-mode acceptance,
#     O_CREAT/O_TRUNC/O_APPEND semantics.
#   - read/lseek: deterministic snapshot read + cursor movement semantics.
#   - write: read-only fd rejection, argument validation, write-back flush
#     on close, append semantics, slot-capacity ENOSPC.
#   - close: valid and invalid-fd behavior (with dirty-slot flush).
#   - fd table saturation: EMFILE when fixed slot table is exhausted.
#   - binary-safe round-trips (DEMO-01 #765): snapshots ride the
#     length-bearing os_fs_read_file_bytes/os_fs_write_file_bytes bridge,
#     so payloads with leading/interior/trailing NUL bytes persist and
#     read back byte-for-byte.
#   - console fds 0/1/2 (slice 3): stdout/stderr write-through to
#     os_console_write with NUL-bounded chunking, stdin read ENOTSUP,
#     ESPIPE lseek, no-op close, /dev/std* open refusal (EBUSY).
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
grep -q "TEST:PASS:clib_posix_fd:open_write_missing_no_creat_enoent" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_maps_denied_to_eacces" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:open_write_mode_accepted" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:write_readonly_fd_ebadf" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:write_null_efault" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:write_zero_returns_zero" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:wronly_write_returns_count" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:wronly_close_flushes" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:wronly_trunc_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:creat_missing_accepted" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:creat_write_roundtrip" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:append_forces_end_of_file" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:enospc_past_slot_capacity" "$LOG_PATH"
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
grep -q "TEST:PASS:clib_posix_fd:console_stdout_forwards_chunk" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_stderr_forwards_chunk" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_stdin_write_ebadf" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_nul_splits_chunks" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_lone_nul_no_call" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_write_failure_eio" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_stdin_read_enotsup" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_lseek_espipe" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_close_noop_success" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_write_after_close_still_works" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_alias_open_ebusy" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:console_alias_stdout_ebusy" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd:symbol_set_pinned" "$LOG_PATH"
grep -q "TEST:PASS:clib_posix_fd$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
