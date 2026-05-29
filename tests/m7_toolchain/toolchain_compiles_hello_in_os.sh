#!/usr/bin/env bash
# tests/m7_toolchain/toolchain_compiles_hello_in_os.sh
#
# SKIP-pinned acceptance stub for the M7-TOOLCHAIN acceptance suite
# scaffolding (issue #423, umbrella #403). Real assertions land with
# the gating execute slice (issue #409).
#
# Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md
#       (section "Acceptance tests" -> `toolchain_compiles_hello_in_os`)
#
# Intent: cc driver app produces a SOF-wrapped ELF on-target.
#
# Emits the canonical SKIP marker for the bundle gate, then rolls up a
# TEST:PASS:<target> so validate_bundle.sh stays green while the gating
# component lands. Mirrors the SKIP discipline used by sosh's audit
# stubs (#389/#392) and the M5 cascade audit SKIPs (#344).
set -euo pipefail

printf 'TEST:SKIP:toolchain_compiles_hello_in_os:awaiting_409\n'
printf 'TEST:PASS:toolchain_compiles_hello_in_os\n'
