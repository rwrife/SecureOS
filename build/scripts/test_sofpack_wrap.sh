#!/usr/bin/env bash
# build/scripts/test_sofpack_wrap.sh
#
# M7-TOOLCHAIN-006 sub-slice (issue #409, plan
# plans/2026-05-28-in-os-toolchain-self-hosting.md Phase 5).
#
# Builds + runs the host unit test for `user/libs/sofpack`, the
# freestanding userland-callable SOF wrapper that the future in-OS `cc`
# driver (#409) calls instead of `sof_build()` (which lives in the
# kernel + drags crypto headers).
#
# The test links libsofpack against kernel/format/sof.c (and the crypto
# stack sof.c pulls in) so the byte-for-byte equivalence with sof_build
# can be asserted on the wire.
#
# Outputs deterministic TEST:PASS markers consumed by
# build/scripts/test.sh.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/sofpack/src/sofpack.c" \
  "$ROOT_DIR/kernel/format/sof.c" \
  "$ROOT_DIR/kernel/crypto/cert.c" \
  "$ROOT_DIR/kernel/crypto/ed25519.c" \
  "$ROOT_DIR/kernel/crypto/sha512.c" \
  "$ROOT_DIR/tests/sofpack_wrap_test.c" \
  -o "$OUT_DIR/sofpack_wrap_test"

LOG_PATH="$OUT_DIR/sofpack_wrap_test.log"
"$OUT_DIR/sofpack_wrap_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:sofpack_wrap:wrap_size_matches_wrap" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:byte_identical_to_sof_build" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:optional_fields_byte_identical" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:parses_back_through_sof_parse" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:rejects_invalid_args" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:buffer_too_small" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap:long_value_clamped" "$LOG_PATH"
grep -q "TEST:PASS:sofpack_wrap$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
