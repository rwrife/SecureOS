#!/usr/bin/env bash
# test_codesign.sh — Compile and run the code signing integration tests.
#
# Compiles codesign_test.c with crypto and SOF sources, then executes
# the resulting binary.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_TEST="$ROOT_DIR/tests/codesign_test.c"
SRC_SOF="$ROOT_DIR/kernel/format/sof.c"
SRC_SHA512="$ROOT_DIR/kernel/crypto/sha512.c"
SRC_ED25519="$ROOT_DIR/kernel/crypto/ed25519.c"
SRC_CERT="$ROOT_DIR/kernel/crypto/cert.c"

OUT="$ROOT_DIR/artifacts/codesign_test"

mkdir -p "$ROOT_DIR/artifacts"

echo "[test_codesign] compiling..."
gcc -std=c11 -Wall -Wextra -Werror -O2 \
    -o "$OUT" \
    "$SRC_TEST" "$SRC_SOF" "$SRC_SHA512" "$SRC_ED25519" "$SRC_CERT"

echo "[test_codesign] running..."
"$OUT"
EXIT_CODE=$?

rm -f "$OUT"

if [ $EXIT_CODE -ne 0 ]; then
  echo "[test_codesign] FAILED (exit=$EXIT_CODE)"
  exit 1
fi

echo "[test_codesign] PASSED"
exit 0