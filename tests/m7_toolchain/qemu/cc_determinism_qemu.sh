#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_determinism_qemu.sh
#
# Pre-#409/#410 SKIP-pinned starter harness for issue #572.
#
# Contract to enforce once the runtime toolchain path lands:
#   1) Build the same sample via in-OS `cc` twice in one boot and once in a
#      fresh boot.
#   2) Assert byte-identical SOF output (`sha256`) across all runs.
#   3) On mismatch, emit a deterministic field-level diff to aid root-cause
#      triage without requiring manual hex inspection.
#   4) Anchor docs/abi/sof-format.md reproducibility contract to this marker.
#
# Gate issues:
#   - #572 tracks the determinism harness itself (this starter scaffold)
#   - #409/#410 remain open (runtime cc path + unsigned-run execution wiring)
set -euo pipefail

printf 'TEST:SKIP:toolchain_cc_determinism:awaiting_409_410\n'
printf 'TEST:SKIP:cc_determinism_qemu:gating_issues=409,410,572\n'
printf 'TEST:PASS:toolchain_cc_determinism\n'
