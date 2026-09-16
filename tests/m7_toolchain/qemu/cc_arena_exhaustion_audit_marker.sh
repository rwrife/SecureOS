#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_arena_exhaustion_audit_marker.sh
#
# Pre-#767/#771 SKIP-pinned harness for the arena-exhaustion contract.
#
# Contract to enforce once the runtime toolchain path lands:
#   1) Stage a synthetic compile input whose memory demand exceeds the
#      runtime.arena_bytes clamp pinned for /apps/dev/cc.
#   2) Assert kernel-side deny evidence:
#      CAP:DENY:<sid>:mem_brk:arena_bytes
#   3) Assert toolchain audit evidence:
#      cc.compile.fail{reason=arena_exhausted,arena_bytes=<n>,requested=<m>}
#   4) Assert cc exits via the v0 "internal" slot (issue #589), not the
#      compile-error slot.
#
# Gate issues:
#   - #404 closed (userland heap substrate landed)
#   - #767/#771 still open (cc runtime path + real guest acceptance)
set -euo pipefail

printf 'TEST:SKIP:toolchain_cc_arena_exhaustion_audit_marker:awaiting_767_771\n'
printf 'TEST:SKIP:cc_arena_exhaustion_audit_marker:gating_issues=404,767,771\n'
printf 'TEST:PASS:toolchain_cc_arena_exhaustion_audit_marker\n'
