#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/kernel/hal/storage_hal.c" \
  "$ROOT_DIR/kernel/drivers/disk/ramdisk.c" \
  "$ROOT_DIR/kernel/fs/fs_service.c" \
  "$ROOT_DIR/tests/fs_service_test.c" \
  -o "$OUT_DIR/fs_service_test"

"$OUT_DIR/fs_service_test"
