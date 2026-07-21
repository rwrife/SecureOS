#!/usr/bin/env bash
# build/scripts/test_cc_manifest_resolution_precedence.sh
#
# Host gate for issue #634:
#   cc manifest precedence resolver (cli > sidecar > synth) + hard-fail
#   behavior for invalid `--manifest` override.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/apps/cc/manifest_resolution.c" \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/m7_toolchain/cc_manifest_resolution_precedence_test.c" \
  -o "$OUT_DIR/cc_manifest_resolution_precedence_test"

LOG_PATH="$OUT_DIR/cc_manifest_resolution_precedence_test.log"
"$OUT_DIR/cc_manifest_resolution_precedence_test" | tee "$LOG_PATH"

grep -q "TEST:PASS:cc_manifest_resolution_precedence:cli_wins" "$LOG_PATH"
grep -q "TEST:PASS:cc_manifest_resolution_precedence:sidecar_used" "$LOG_PATH"
grep -q "TEST:PASS:cc_manifest_resolution_precedence:synth_fallback" "$LOG_PATH"
grep -q "TEST:PASS:cc_manifest_resolution_precedence:invalid_cli_hard_fail" "$LOG_PATH"
grep -q "TEST:PASS:cc_manifest_resolution_precedence$" "$LOG_PATH"
! grep -q "TEST:FAIL:" "$LOG_PATH"
