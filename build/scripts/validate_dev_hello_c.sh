#!/usr/bin/env bash
# build/scripts/validate_dev_hello_c.sh
#
# Drift gate for the canonical in-OS toolchain sample source `dev/hello.c`
# (issue #636). Recomputes the source SHA-256 and compares it against the
# checked-in pin in tests/host/pins/dev_hello_c.sha256.
#
# Why this exists:
#   `dev/hello.c` is the input anchor for M7 toolchain validation. A tiny
#   unreviewed edit to this file can invalidate downstream host/qemu goldens.
#   This gate makes source drift explicit and reviewable.
#
# Sub-markers:
#   - dev_hello_c_pin_file_present
#   - dev_hello_c_source_present
#   - dev_hello_c_sha_matches_pin
#   - dev_hello_c_pin

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PIN_REL="tests/host/pins/dev_hello_c.sha256"
SRC_REL="dev/hello.c"
PIN_FILE="$ROOT_DIR/$PIN_REL"
SRC_FILE="$ROOT_DIR/$SRC_REL"

emit() { printf '%s\n' "$1"; }
pass() { emit "TEST:PASS:$1"; }
fail() { emit "TEST:FAIL:$1:$2"; ALL_OK=0; }

ALL_OK=1

if [[ -f "$PIN_FILE" ]]; then
  pass "dev_hello_c_pin_file_present"
else
  fail "dev_hello_c_pin_file_present" "missing:$PIN_REL"
  emit "TEST:FAIL:dev_hello_c_pin:pin_missing"
  exit 1
fi

if [[ -f "$SRC_FILE" ]]; then
  pass "dev_hello_c_source_present"
else
  fail "dev_hello_c_source_present" "missing:$SRC_REL"
  emit "TEST:FAIL:dev_hello_c_pin:source_missing"
  exit 1
fi

PIN_LINE="$(awk 'NF && $1 !~ /^#/ { print; exit }' "$PIN_FILE")"
if [[ -z "$PIN_LINE" ]]; then
  fail "dev_hello_c_sha_matches_pin" "pin_file_empty"
  emit "TEST:FAIL:dev_hello_c_pin:empty_pin"
  exit 1
fi

EXPECTED_HASH="$(printf '%s\n' "$PIN_LINE" | awk '{print $1}')"
PIN_PATH="$(printf '%s\n' "$PIN_LINE" | awk '{print $2}')"
if [[ -z "$EXPECTED_HASH" || -z "$PIN_PATH" ]]; then
  fail "dev_hello_c_sha_matches_pin" "invalid_pin_format"
  emit "TEST:FAIL:dev_hello_c_pin:invalid_pin_format"
  exit 1
fi
if [[ "$PIN_PATH" != "$SRC_REL" ]]; then
  fail "dev_hello_c_sha_matches_pin" "pin_path_mismatch:$PIN_PATH"
  emit "TEST:FAIL:dev_hello_c_pin:pin_path_mismatch"
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  ACTUAL_HASH="$(sha256sum "$SRC_FILE" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  ACTUAL_HASH="$(shasum -a 256 "$SRC_FILE" | awk '{print $1}')"
else
  fail "dev_hello_c_sha_matches_pin" "sha256_tool_not_found"
  emit "TEST:FAIL:dev_hello_c_pin:sha256_tool_not_found"
  exit 1
fi

if [[ "$EXPECTED_HASH" == "$ACTUAL_HASH" ]]; then
  pass "dev_hello_c_sha_matches_pin"
else
  fail "dev_hello_c_sha_matches_pin" "expected=$EXPECTED_HASH:actual=$ACTUAL_HASH"
fi

if [[ "$ALL_OK" -eq 1 ]]; then
  pass "dev_hello_c_pin"
  exit 0
fi

emit "TEST:FAIL:dev_hello_c_pin:sub_checks_failed"
exit 1
