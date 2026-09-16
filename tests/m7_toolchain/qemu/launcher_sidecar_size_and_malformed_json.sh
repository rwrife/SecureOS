#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launcher_sidecar_size_and_malformed_json.sh
#
# Pre-#771 SKIP-pinned harness for the malformed-sidecar contract.
#
# Contract to enforce when #771 lands and this marker flips from SKIP to PASS:
#   1) Sidecar > MAX_SIDECAR_BYTES is denied.
#   2) Truncated JSON sidecar is denied.
#   3) Trailing-garbage JSON sidecar is denied.
#   4) Empty sidecar is denied.
#   5) Each deny emits canonical CAP:DENY launch evidence for
#      sidecar size/parse rejection (marker naming coordinated with #587).
#
# Normative references:
#   - issue #602 (size+malformed sidecar SKIP harness contract)
#   - issue #771 (runtime flip target)
set -euo pipefail

printf 'TEST:SKIP:toolchain_launcher_sidecar_size_and_malformed_json:awaiting_771\n'
printf 'TEST:PASS:toolchain_launcher_sidecar_size_and_malformed_json\n'
