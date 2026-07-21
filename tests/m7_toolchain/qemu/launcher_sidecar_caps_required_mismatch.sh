#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launcher_sidecar_caps_required_mismatch.sh
#
# Pre-#410 SKIP-pinned harness for issue #605.
#
# Contract to enforce when #410 lands and this marker flips from SKIP to PASS:
#   1) Stage a local SOF + sidecar pair where sidecar caps_required is a
#      strict subset of SOF caps_required; assert launcher deny.
#   2) Stage a pair where sidecar caps_required is a strict superset
#      (escalation); assert launcher deny.
#   3) Stage a pair where sidecar caps_required is disjoint from SOF;
#      assert launcher deny.
#   4) Assert each deny emits canonical launch CAP:DENY evidence for
#      caps_required mismatch (shape coordinated with issue #587 / #594).
#
# Normative references:
#   - issue #605 (caps_required mismatch SKIP harness contract)
#   - issue #410 (runtime flip target)
set -euo pipefail

printf 'TEST:SKIP:toolchain_launcher_sidecar_caps_required_mismatch:awaiting_410\n'
printf 'TEST:PASS:toolchain_launcher_sidecar_caps_required_mismatch\n'
