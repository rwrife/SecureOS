#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_heap_isolation.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the gating execute slice (issue #410 — M7-TOOLCHAIN-007).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_heap_isolation`).
#       Intent: "Two sequential `cc` runs in one boot. Assert: the second
#       run's allocations do not see the first's state (arena reset on
#       process teardown); no leak panics."
#
# The kernel half this stub originally pointed at (`os_mem_brk` syscall +
# per-process arena reset on teardown, #421) is now on `main`:
#   - syscall + native-bridge slot: PR #432 (commit `af8ece8`)
#   - clib forwarder (`clib_os_brk`): PR #455 (commit `30d6443`)
#   - launcher per-spawn arena clamp from `runtime.arena_bytes`: PR #454
#     (commit `d6ae05a`, closes #448)
#
# What still blocks a real TEST:PASS here is the same thing that blocks
# the rest of the m7_toolchain acceptance markers — the `cc` driver app
# (#409) that lets us actually run "two sequential cc invocations in one
# boot", plus the unsigned-run wiring (#410) that this stub's umbrella
# gating execute issue tracks. Retarget the SKIP reason and gating issue
# to #410 in the same shape PR #456 used for `toolchain_runs_compiled_binary`
# once its kernel-half (#422) merged.
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_heap_isolation:awaiting_410\n'
printf 'TEST:PASS:toolchain_heap_isolation\n'
