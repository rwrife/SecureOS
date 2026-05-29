#!/usr/bin/env bash
# test_in_os_toolchain_dev_dir.sh
#
# Phase 1 of the in-OS toolchain (plan
# plans/2026-05-28-in-os-toolchain-self-hosting.md). Verifies that the
# /apps/dev developer directory and its sample/guide are staged onto the
# disk image correctly by tools/populate_disk_image.py.
#
# Host-only: no QEMU, no cross-toolchain — just python3.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${PYTHON:-python3}"

if ! command -v "$PY" >/dev/null 2>&1; then
  echo "TEST:FAIL:in_os_toolchain_dev_dir: python3 not found" >&2
  exit 78
fi

"$PY" "$ROOT_DIR/tests/in_os_toolchain_dev_dir_test.py"
