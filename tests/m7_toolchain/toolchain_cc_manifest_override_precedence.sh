#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_cc_manifest_override_precedence.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #609.
# Delegates to qemu-scoped harness logic while preserving the canonical
# tests/m7_toolchain/<marker>.sh dispatch contract.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_cc_manifest_override_precedence
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/cc_manifest_override_precedence.sh"
