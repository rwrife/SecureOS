#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_cc_version_and_help_text_pinned.sh
#
# M7-TOOLCHAIN acceptance harness entrypoint for issue #637.
# Delegates to the qemu-scoped harness implementation so build/scripts/test.sh
# can keep using tests/m7_toolchain/<marker>.sh dispatch.
# Literal marker anchor for tools/validate_m7_markers.py:
# TEST:PASS:toolchain_cc_version_and_help_text_pinned
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bash "$ROOT_DIR/tests/m7_toolchain/qemu/cc_version_and_help_text_pinned.sh"
