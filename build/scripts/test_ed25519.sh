#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/crypto/sha512.c" \
  "$ROOT_DIR/kernel/crypto/ed25519.c" \
  "$ROOT_DIR/tests/ed25519_test.c" \
  -o "$OUT_DIR/ed25519_test"

"$OUT_DIR/ed25519_test"

# RFC 8032 §7.1 known-answer vectors (issue #137).  Run as a second test
# binary so a KAT regression surfaces under its own TEST:PASS:ed25519_kat
# marker without losing the existing ed25519 self-roundtrip coverage.
cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/crypto/sha512.c" \
  "$ROOT_DIR/kernel/crypto/ed25519.c" \
  "$ROOT_DIR/tests/ed25519_kat_test.c" \
  -o "$OUT_DIR/ed25519_kat_test"

"$OUT_DIR/ed25519_kat_test"