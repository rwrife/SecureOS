#!/usr/bin/env bash
# build/scripts/test_clib_stdio_fdopen.sh
#
# Build + run the stdio<->posix_fd fdopen adoption host gate
# (issue #766, DEMO-02 TinyCC link slice).
#
# Links the REAL shipping shape (stdio.c + posix_fd.c + errno.c) so the
# weak-forwarder wiring (clib_stdio_fd_path_fn / _forget_fn /
# _close_fn) resolves strongly and the fdopen contract TinyCC's tccelf.c
# depends on is exercised end to end:
#   open(O_WRONLY|O_CREAT|O_TRUNC) -> fdopen("wb") -> fwrite -> fclose
# persists the payload exactly once, and a dirty fd snapshot can never
# truncate the FILE's output.
#
# Compiled with -fno-builtin so the assertions exercise clib's fdopen /
# fopen / fread / fwrite implementations rather than host-libc
# shortcuts.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/errno.c" \
  "$ROOT_DIR/user/libs/clib/src/posix_fd.c" \
    "$ROOT_DIR/user/libs/clib/src/stdio.c" \
  "$ROOT_DIR/tests/clib_stdio_fdopen_test.c" \
  -I"$ROOT_DIR/user/libs/clib/include" \
  -I"$ROOT_DIR/user/include" \
  -o "$OUT_DIR/clib_stdio_fdopen_test"

LOG_PATH="$OUT_DIR/clib_stdio_fdopen_test.log"
"$OUT_DIR/clib_stdio_fdopen_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdio_fdopen:tinycc_open_creat"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fdopen_wb_nonnull"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fwrite_full_count"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fclose_ok"                            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:payload_persisted_exact"              "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fclose_closed_adopted_fd"             "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:alpha_open_rdwr"                      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:alpha_fd_write_dirty"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fdopen_after_dirty_write"             "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fclose_forget_case"                   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:stale_fd_snapshot_never_flushed"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:alpha_open_rdonly"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fdopen_r_nonnull"                     "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fread_full_snapshot"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:fd_closed_by_fclose_r"                "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:mode_rplus_rejected"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:mode_empty_rejected"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:mode_null_rejected"                   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:bad_fd_rejected"                      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:console_fd_rejected"                  "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:path_live_fd"                         "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:path_invalid_fd_null"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:forget_invalid_fd_ebadf"              "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:forget_after_close_ebadf"             "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:forget_dirty_fd_ok"                   "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:forget_close_wrote_nothing"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen:symbol_set_pinned"                    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio_fdopen$"                                     "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
