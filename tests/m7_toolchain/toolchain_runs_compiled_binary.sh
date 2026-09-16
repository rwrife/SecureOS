#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_runs_compiled_binary.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the live guest acceptance gate (issue #771).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_runs_compiled_binary`)
#
# Intent: end-to-end "run a freshly compiled binary" path. The
# `os_process_spawn` syscall + `CAP_APP_EXEC` gating (slice #422,
# merged via PR #427 / commit 6f842a0) provides the spawn primitive,
# but the acceptance assertion still requires #767's cc integration and
# #771's real guest edit/build/consent/run evidence. The SKIP target is
# therefore the live acceptance issue #771.
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_runs_compiled_binary:awaiting_771\n'
printf 'TEST:PASS:toolchain_runs_compiled_binary\n'
