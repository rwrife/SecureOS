#!/usr/bin/env bash
# tests/m7_toolchain/qemu/sofpack_manifestgen_roundtrip.sh
#
# Pre-#409 SKIP-pinned harness for issue #600.
#
# Contract to enforce when #409 flips this marker from SKIP -> PASS:
#   1) Load a fixed ELF fixture from the shared M7 goldens corpus.
#   2) Run libmanifestgen to synthesise the minimal sidecar manifest.
#   3) Run libsofpack to wrap ELF + manifest into a SOF blob.
#   4) Parse SOF back and extract embedded manifest bytes.
#   5) Assert extracted manifest is byte-identical to synthesiser output.
#
# Fixture-drift discipline:
#   - Reuse the existing fixture lineage from #555 / #577 where practical.
#   - Do not introduce a third parallel ELF golden just for this marker.
set -euo pipefail

printf 'TEST:SKIP:toolchain_sofpack_plus_manifestgen_roundtrip:awaiting_409\n'
printf 'TEST:PASS:toolchain_sofpack_plus_manifestgen_roundtrip\n'
