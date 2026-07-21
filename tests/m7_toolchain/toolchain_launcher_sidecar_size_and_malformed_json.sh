#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_launcher_sidecar_size_and_malformed_json.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #602.
# Delegates to qemu-scoped harness logic while preserving the canonical
# tests/m7_toolchain/<marker>.sh dispatch contract.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_launcher_sidecar_size_and_malformed_json
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/launcher_sidecar_size_and_malformed_json.sh"
