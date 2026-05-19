#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/netlib/entropy.c" \
  "$ROOT_DIR/user/libs/netlib/ca_bundle.c" \
  "$ROOT_DIR/tests/tls_test.c" \
  -o "$OUT_DIR/tls_test"

"$OUT_DIR/tls_test"