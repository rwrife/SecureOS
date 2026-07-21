#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_missing_manifest_sidecar.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #596.
# Delegates to qemu-scoped harness logic while preserving the canonical
# tests/m7_toolchain/<marker>.sh dispatch contract.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_missing_manifest_sidecar
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/toolchain_missing_manifest_sidecar.sh"
