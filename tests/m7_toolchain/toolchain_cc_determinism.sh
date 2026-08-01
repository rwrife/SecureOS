#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_cc_determinism.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #572.
# Delegates to the qemu-scoped harness while preserving the canonical
# tests/m7_toolchain/<marker>.sh dispatch contract.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_cc_determinism
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/cc_determinism_qemu.sh"
