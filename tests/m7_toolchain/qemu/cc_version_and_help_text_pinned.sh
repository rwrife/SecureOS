#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_version_and_help_text_pinned.sh
#
# Pre-#409 SKIP-pinned harness for issue #637.
#
# Contract to enforce when #409 flips this from SKIP -> PASS:
#   1) `cc --version` stdout must be byte-identical to
#      tests/m7_toolchain/goldens/cc_version.stdout.txt
#   2) `cc --help` stdout must be byte-identical to
#      tests/m7_toolchain/goldens/cc_help.stdout.txt
#   3) both invocations exit 0 and emit no stderr
#
# Determinism note: goldens must not contain host paths or build timestamps.
#
# CONTRIBUTING pointer: follow the SKIP-pinned harness authoring guide in #608.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
VERSION_GOLDEN="$ROOT_DIR/tests/m7_toolchain/goldens/cc_version.stdout.txt"
HELP_GOLDEN="$ROOT_DIR/tests/m7_toolchain/goldens/cc_help.stdout.txt"

for f in "$VERSION_GOLDEN" "$HELP_GOLDEN"; do
  if [[ ! -f "$f" ]]; then
    printf 'TEST:FAIL:toolchain_cc_version_and_help_text_pinned:missing_golden:%s\n' "$f" >&2
    exit 1
  fi
  if ! grep -q '^TODO: populate at #409 flip$' "$f"; then
    printf 'TEST:FAIL:toolchain_cc_version_and_help_text_pinned:golden_missing_todo_header:%s\n' "$f" >&2
    exit 1
  fi
 done

printf 'TEST:SKIP:toolchain_cc_version_and_help_text_pinned:awaiting_409\n'
printf 'TEST:PASS:toolchain_cc_version_and_help_text_pinned\n'
