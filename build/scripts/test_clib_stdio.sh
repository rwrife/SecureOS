#!/usr/bin/env bash
# build/scripts/test_clib_stdio.sh
#
# Build + run the freestanding <stdio.h> host unit test
# (issue #447 / #407 — M7-TOOLCHAIN-004 slice 8, plan
#  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
#
# Covers:
#   - printf_basic_format            : %s %d round-trip exact bytes
#   - printf_full_spec_set           : %u %x %p %c %% %ld %lu, width, zero-pad
#   - fopen_fwrite_fread_round_trip  : "/tmp/foo" 64-byte payload
#   - large_payload_round_trip       : >4 KiB payload (4097 bytes)
#   - stderr_routes_to_console       : fprintf(stderr,...) hits console sink
#   - fopen_invalid_mode_returns_null
#   - defensive_no_backend           : no backend → fopen("r") returns NULL
#   - shutdown_resets_pool               : after shutdown, full pool available
#   - snprintf_basic                     : full-fits + bytes + NUL
#   - snprintf_truncation                : returns full len, terminates at size-1
#   - snprintf_size_zero_sizing_probe    : (NULL,0) returns required length
#   - snprintf_exact_fit                 : size == strlen+1
#   - snprintf_null_fmt_returns_negative : NULL fmt → -1
#   - vsnprintf_matches_snprintf         : va_list path mirrors variadic
#   - sprintf_basic                      : sprintf formatting + return count pin
#   - symbol_set_pinned              : drift guard
#
# Compiled with `-fno-builtin` so the assertions exercise OUR
# printf / fopen / fwrite implementations rather than any host-libc
# shortcut. The test driver links the clib stdio.c TU directly; its
# canonical libc symbol names (fopen, fread, ...) override the host
# libc references via the static-link-first-wins rule that the str/
# mem (PR #416), ctype (PR #417), qsort (PR #418), stdlib (PR #428),
# errno (PR #430), and stdarg (PR #431) slices already rely on.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  "$ROOT_DIR/user/libs/clib/src/stdio.c" \
  "$ROOT_DIR/tests/clib_stdio_test.c" \
  -o "$OUT_DIR/clib_stdio_test"

LOG_PATH="$OUT_DIR/clib_stdio_test.log"
"$OUT_DIR/clib_stdio_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:clib_stdio:printf_basic_format"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:printf_full_spec_set"          "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:fopen_fwrite_fread_round_trip" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:large_payload_round_trip"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:stderr_routes_to_console"      "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:fopen_invalid_mode_returns_null" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:defensive_no_backend"          "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:shutdown_resets_pool"          "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:snprintf_basic"                "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:snprintf_truncation"           "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:snprintf_size_zero_sizing_probe" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:snprintf_exact_fit"            "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:snprintf_null_fmt_returns_negative" "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:vsnprintf_matches_snprintf"    "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:sprintf_basic"                 "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio:symbol_set_pinned"             "$LOG_PATH"
grep -q "TEST:PASS:clib_stdio$"                              "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
