#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/tests/netlib_url_scheme_test.c" \
  "$ROOT_DIR/user/libs/netlib/url_scheme.c" \
  -I "$ROOT_DIR/user/libs/netlib" \
  -o "$OUT_DIR/netlib_url_scheme_test"

"$OUT_DIR/netlib_url_scheme_test"
