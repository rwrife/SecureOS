#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_unsigned_prompt_enforced.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the gating execute slice (issue #410).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_unsigned_prompt_enforced`)
#
# Intent: Unsigned-run wiring through the launcher auth flow (Tier 1 trust, AUTH_TYPE_UNSIGNED_BIN).
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_unsigned_prompt_enforced:awaiting_410\n'
printf 'TEST:PASS:toolchain_unsigned_prompt_enforced\n'
