#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_cc_manifest_sidecar_written_on_link.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #634.
# Delegates to the qemu-scoped harness implementation.
# TEST:PASS:toolchain_cc_manifest_sidecar_written_on_link
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/cc_manifest_sidecar_written_on_link.sh"
