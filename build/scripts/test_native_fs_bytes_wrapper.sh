#!/usr/bin/env bash
# @file test_native_fs_bytes_wrapper.sh
# @brief Issue #765 (DEMO-01) — host-side round-trip gate for the
#        binary-safe fs wrappers `os_fs_read_file_bytes` /
#        `os_fs_write_file_bytes`.
#
# Compiles tests/native_fs_bytes_wrapper_test.c with the user runtime
# stubs and runs it. The test maps a synthetic native bridge page so the
# wrappers are driven dynamically on host: exact-byte payload round-trips
# with embedded NULs, the v5 version-handshake gate (a pre-v5 page must
# degrade to the no-bridge fall-through), every return-code mapping, and
# the argument guards.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$ROOT_DIR/user/include" \
  "$ROOT_DIR/tests/native_fs_bytes_wrapper_test.c" \
  "$ROOT_DIR/user/runtime/secureos_api_stubs.c" \
  -o "$OUT_DIR/native_fs_bytes_wrapper_test"

LOG_PATH="$OUT_DIR/native_fs_bytes_wrapper_test.log"
"$OUT_DIR/native_fs_bytes_wrapper_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:native_fs_bytes_wrapper:read_null_out_len_early_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_zero_capacity_early_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_null_path_early_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_null_content_early_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_null_path_early_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:pre_v5_bridge_degrades_to_no_bridge_read" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:pre_v5_bridge_degrades_to_no_bridge_write" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_exact_bytes_with_nuls" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_capacity_denied_maps_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_rc2_maps_not_found" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:read_rc1_maps_denied" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_exact_bytes_append0" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_append_flag_passthrough" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_zero_len_ok" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_rc1_maps_denied" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper:write_rc3_maps_error" "$LOG_PATH"
grep -q "TEST:PASS:native_fs_bytes_wrapper$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
