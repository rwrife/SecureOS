#!/usr/bin/env bash
# build/scripts/test_manifest_sidecar_suffix.sh
#
# Drift gate for the `<binary>.manifest.json` sidecar filename convention
# (issue #580). Asserts:
#   1) docs/abi/manifest.md contains the exact quoted suffix ".manifest.json"
#   2) manifest_default.h exports #define MANIFEST_SIDECAR_SUFFIX "..."
#   3) doc and header values agree byte-for-byte
#
# Also compiles/runs tests/manifest_sidecar_suffix_test.c so the header
# contract is exercised by a host binary.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DOC="$ROOT_DIR/docs/abi/manifest.md"
HDR="$ROOT_DIR/user/libs/manifestgen/include/manifestgen/manifest_default.h"
OUT_DIR="$ROOT_DIR/artifacts/tests"
BIN="$OUT_DIR/manifest_sidecar_suffix_test"
LOG="$OUT_DIR/manifest_sidecar_suffix_test.log"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/tests/manifest_sidecar_suffix_test.c" \
  -o "$BIN"

"$BIN" | tee "$LOG"

grep -q '^TEST:PASS:manifest_sidecar_suffix$' "$LOG"
! grep -q '^TEST:FAIL:' "$LOG"

if ! grep -Fq '".manifest.json"' "$DOC"; then
  echo "TEST:FAIL:manifest_sidecar_suffix:abi_doc_missing_suffix" >&2
  exit 1
fi
echo "TEST:PASS:manifest_sidecar_suffix:abi_doc"

HEADER_SUFFIX="$({ sed -n 's/^#define[[:space:]]\+MANIFEST_SIDECAR_SUFFIX[[:space:]]\+"\([^"]\+\)".*/\1/p' "$HDR" || true; } | head -n1)"
if [[ -z "$HEADER_SUFFIX" ]]; then
  echo "TEST:FAIL:manifest_sidecar_suffix:header_define_missing" >&2
  exit 1
fi
if [[ "$HEADER_SUFFIX" != ".manifest.json" ]]; then
  echo "TEST:FAIL:manifest_sidecar_suffix:header_define_value:$HEADER_SUFFIX" >&2
  exit 1
fi
echo "TEST:PASS:manifest_sidecar_suffix:header_define"

DOC_SUFFIX="$({ grep -F '".manifest.json"' "$DOC" || true; } | head -n1 | sed -n 's/.*"\(\.manifest\.json\)".*/\1/p')"
if [[ "$DOC_SUFFIX" != "$HEADER_SUFFIX" ]]; then
  echo "TEST:FAIL:manifest_sidecar_suffix:disagree:doc=$DOC_SUFFIX:header=$HEADER_SUFFIX" >&2
  exit 1
fi
echo "TEST:PASS:manifest_sidecar_suffix:agree"

echo "TEST:PASS:manifest_sidecar_suffix"
