#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_sofpack_plus_manifestgen_roundtrip.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #600.
# Delegates to qemu-scoped harness logic while preserving the canonical
# tests/m7_toolchain/<marker>.sh dispatch contract.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_sofpack_plus_manifestgen_roundtrip
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/sofpack_manifestgen_roundtrip.sh"
