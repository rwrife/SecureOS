#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_launcher_manifest_ownership_role_enforced.sh
#
# M7 marker entrypoint for issue #597. Delegates to the qemu harness so
# pre-#585 SKIP semantics remain centralized.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_launcher_manifest_ownership_role_enforced

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/launcher_manifest_ownership_role_enforced.sh"
