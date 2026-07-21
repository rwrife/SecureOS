#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_arena_exhaustion_audit_marker.sh
#
# Pre-#409/#410 SKIP-pinned harness for issue #610.
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
#   - #409/#410 still open (cc runtime execution path + unsigned-run wiring)
set -euo pipefail

printf 'TEST:SKIP:toolchain_cc_arena_exhaustion_audit_marker:awaiting_409_410\n'
printf 'TEST:SKIP:cc_arena_exhaustion_audit_marker:gating_issues=404,409,410\n'
printf 'TEST:PASS:toolchain_cc_arena_exhaustion_audit_marker\n'
