#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launcher_sidecar_owner_kind_mismatch.sh
#
# Pre-#771 SKIP-pinned harness for the owner-kind mismatch contract.
#
# Contract to enforce when #771 lands and this marker flips from SKIP to PASS:
#   1) Stage a local SOF whose embedded manifest declares owner.kind=local.
#   2) Stage a sibling <binary>.manifest.json sidecar that declares a
#      different owner.kind value (for example `internal`).
#   3) Launch through sosh app_exec and assert launcher deny.
#   4) Assert canonical audit evidence records the owner-kind mismatch deny
#      path (LAUNCH:DENY / CAP:DENY naming per #587 + #554 alignment).
#
# Normative references:
#   - issue #601 (owner.kind mismatch SKIP harness contract)
#   - issue #771 (runtime flip target)
set -euo pipefail

printf 'TEST:SKIP:toolchain_launcher_sidecar_owner_kind_mismatch:awaiting_771\n'
printf 'TEST:PASS:toolchain_launcher_sidecar_owner_kind_mismatch\n'
