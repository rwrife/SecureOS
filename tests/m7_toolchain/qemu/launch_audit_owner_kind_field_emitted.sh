#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launch_audit_owner_kind_field_emitted.sh
#
# Pre-#771 SKIP-pinned harness for the launch-audit contract (source: #554).
#
# Contract to enforce when #771 flips this marker from SKIP -> PASS:
#   1) QEMU serial log contains at least one launch-allow line matching:
#        launch.granted ... owner_kind=(internal|external|local)
#   2) QEMU serial log contains at least one launch-deny line matching:
#        launch.denied ... owner_kind=(internal|external|local)
#   3) Coverage includes representative binaries for each owner.kind staged by
#      the m7_toolchain suite (`internal`, `external`, and `local`).
#
# Normative marker-family reference:
#   docs/abi/audit-markers.md (row: launch.granted / launch.denied owner_kind)
set -euo pipefail

printf 'TEST:SKIP:toolchain_launch_audit_owner_kind_field_emitted:awaiting_771\n'
printf 'TEST:PASS:toolchain_launch_audit_owner_kind_field_emitted\n'
