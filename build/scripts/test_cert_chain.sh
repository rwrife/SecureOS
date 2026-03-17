#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/crypto/sha512.c" \
  "$ROOT_DIR/kernel/crypto/ed25519.c" \
  "$ROOT_DIR/kernel/crypto/cert.c" \
  "$ROOT_DIR/tests/cert_chain_test.c" \
  -o "$OUT_DIR/cert_chain_test"

"$OUT_DIR/cert_chain_test"