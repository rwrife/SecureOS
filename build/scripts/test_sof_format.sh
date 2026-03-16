#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/format/sof.c" \
  "$ROOT_DIR/tests/sof_format_test.c" \
  -o "$OUT_DIR/sof_format_test"

"$OUT_DIR/sof_format_test"