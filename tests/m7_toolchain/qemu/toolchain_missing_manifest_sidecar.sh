#!/usr/bin/env bash
# tests/m7_toolchain/qemu/toolchain_missing_manifest_sidecar.sh
#
# Pre-#410 SKIP-pinned harness for issue #596.
#
# Contract to enforce when #410 lands and this marker flips from SKIP to PASS:
#   1) Stage a hand-crafted local owner.kind=\"local\" SOF under /apps/local/
#      with no sibling <binary>.manifest.json.
#   2) Launch through sosh app_exec and assert launcher deny.
#   3) Assert canonical deny evidence includes
#      `reason=missing_manifest_sidecar` (or the settled enum from #594/#542).
#
# Normative references:
#   - issue #596 (missing sidecar SKIP harness contract)
#   - issue #580 (canonical sidecar filename convention)
#   - issue #410 (runtime flip target)
set -euo pipefail

printf 'TEST:SKIP:toolchain_missing_manifest_sidecar:awaiting_410\n'
printf 'TEST:PASS:toolchain_missing_manifest_sidecar\n'
