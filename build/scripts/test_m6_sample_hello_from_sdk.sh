#!/usr/bin/env bash
# @file test_m6_sample_hello_from_sdk.sh
# @brief Host-side fixture gate for issue #584 (M6-SDK-004 starter slice).
#
# Asserts:
#   - sample builds against SDK/libos source composition used by M6-SDK-002
#   - sample manifest validates and pins owner.kind=external intent
#
# Emits:
#   TEST:PASS:m6_sample_hello_from_sdk:builds_against_sdk_only
#   TEST:PASS:m6_sample_hello_from_sdk:manifest_validates
#   TEST:PASS:m6_sample_hello_from_sdk
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests/m6_sample_hello_from_sdk"
mkdir -p "$OUT_DIR"

BUILD_SCRIPT="$ROOT_DIR/build/scripts/build_sample_hello_from_sdk.sh"
DRIVER_SRC="$ROOT_DIR/tests/m6_sample_hello_from_sdk_test.c"
DRIVER_BIN="$OUT_DIR/m6_sample_hello_from_sdk_test"

if [[ ! -r "$BUILD_SCRIPT" ]]; then
  echo "TEST:FAIL:harness_missing_script:$BUILD_SCRIPT" >&2
  exit 78
fi
if [[ ! -r "$DRIVER_SRC" ]]; then
  echo "TEST:FAIL:harness_missing_test_source:$DRIVER_SRC" >&2
  exit 78
fi

OUT_LOG="$OUT_DIR/build_sample_hello_from_sdk.log"
set +e
bash "$BUILD_SCRIPT" >"$OUT_LOG" 2>&1
BUILD_RC=$?
set -e
cat "$OUT_LOG"

if [[ "$BUILD_RC" -ne 0 ]]; then
  echo "TEST:FAIL:m6_sample_hello_from_sdk:build_script_failed" >&2
  exit 1
fi

if grep -q '^BUILD_SAMPLE_HELLO_FROM_SDK:SKIP:host_arch_not_x86_64:' "$OUT_LOG"; then
  ARCH="$(grep '^BUILD_SAMPLE_HELLO_FROM_SDK:SKIP:host_arch_not_x86_64:' "$OUT_LOG" | tail -n1 | awk -F: '{print $NF}')"
  echo "TEST:SKIP:m6_sample_hello_from_sdk:host_arch_not_x86_64:$ARCH"
  exit 0
fi

CC="${CC:-cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "TEST:FAIL:harness_missing_tool:$CC" >&2
  exit 78
fi

"$CC" -std=c11 -Wall -Wextra -Werror "$DRIVER_SRC" -o "$DRIVER_BIN"

"$DRIVER_BIN" \
  "$ROOT_DIR/samples/hello-from-sdk/main.c" \
  "$ROOT_DIR/samples/hello-from-sdk/manifest.json" \
  "$OUT_DIR/hello_from_sdk.nm.txt"
