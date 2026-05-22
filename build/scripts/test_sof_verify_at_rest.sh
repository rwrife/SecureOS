#!/usr/bin/env bash
# test_sof_verify_at_rest.sh — Compile and run the SOF verify-at-rest tests (#138).
#
# Builds tests/sof_verify_at_rest_test.c against the same crypto + SOF
# sources that codesign_test.sh links, then runs the binary. The test
# writes a signed SOF blob to a temp file, re-reads it, and asserts that
# sof_verify_signature accepts the unmodified persisted bytes and rejects
# single-byte mutations in both the signature region and the payload
# region — the at-rest counterpart of the in-memory roundtrip exercised
# by tests/codesign_test.c.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_TEST="$ROOT_DIR/tests/sof_verify_at_rest_test.c"
SRC_SOF="$ROOT_DIR/kernel/format/sof.c"
SRC_SHA512="$ROOT_DIR/kernel/crypto/sha512.c"
SRC_ED25519="$ROOT_DIR/kernel/crypto/ed25519.c"
SRC_CERT="$ROOT_DIR/kernel/crypto/cert.c"

OUT_DIR="$ROOT_DIR/artifacts/tests"
OUT="$OUT_DIR/sof_verify_at_rest_test"
TMP_SOF="$OUT_DIR/sof_verify_at_rest.sof"

mkdir -p "$OUT_DIR"

echo "[test_sof_verify_at_rest] compiling..."
cc -std=c11 -Wall -Wextra -Werror -O2 \
    -o "$OUT" \
    "$SRC_TEST" "$SRC_SOF" "$SRC_SHA512" "$SRC_ED25519" "$SRC_CERT"

echo "[test_sof_verify_at_rest] running..."
"$OUT" "$TMP_SOF"
EXIT_CODE=$?

rm -f "$TMP_SOF"

exit $EXIT_CODE
