#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

"$ROOT_DIR/build/scripts/build_kernel_image.sh"
"$ROOT_DIR/build/scripts/build_disk_image.sh"
"$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_filedemo
"$ROOT_DIR/build/scripts/run_qemu.sh" --test kernel_persistence
