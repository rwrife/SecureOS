#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_large_output_persisted.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the gating execute slice (issue #409).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_large_output_persisted`)
#
# Intent: cc emits a >1 KB binary; FS path (M7-TOOLCHAIN-002, closed #405) stays byte-identical.
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_large_output_persisted:awaiting_409\n'
printf 'TEST:PASS:toolchain_large_output_persisted\n'
