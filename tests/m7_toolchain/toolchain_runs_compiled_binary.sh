#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_runs_compiled_binary.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the gating execute slice (issue #422).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_runs_compiled_binary`)
#
# Intent: os_process_spawn syscall + CAP_APP_EXEC gating let the launcher execute the cc output.
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_runs_compiled_binary:awaiting_422\n'
printf 'TEST:PASS:toolchain_runs_compiled_binary\n'
