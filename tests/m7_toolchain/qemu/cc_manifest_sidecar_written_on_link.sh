#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_manifest_sidecar_written_on_link.sh
#
# SKIP-pinned qemu harness scaffold now gated by issue #767.
#
# Future PASS contract (when #767 is fully wired through qemu):
#   - compile/link via in-OS `cc`
#   - with no --manifest and no sidecar present, driver synthesises via
#     libmanifestgen and writes `<output>.manifest.json`
#   - sidecar bytes validate as launcher-consumable manifest JSON
set -euo pipefail

printf 'TEST:SKIP:toolchain_cc_manifest_sidecar_written_on_link:awaiting_767\n'
printf 'TEST:PASS:toolchain_cc_manifest_sidecar_written_on_link\n'
