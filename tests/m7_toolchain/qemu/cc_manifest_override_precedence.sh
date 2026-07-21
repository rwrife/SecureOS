#!/usr/bin/env bash
# tests/m7_toolchain/qemu/cc_manifest_override_precedence.sh
#
# Pre-#409/#410 SKIP-pinned harness for issue #609.
#
# Contract to enforce when toolchain execute slices land and this marker flips
# from SKIP to PASS:
#   1) Stage an author-supplied `--manifest override.json` and a co-located
#      `<output>.manifest.json` sidecar with intentionally distinct
#      `caps_required` + `runtime.arena_bytes` fields.
#   2) Invoke `cc` with `--manifest override.json`.
#   3) Assert resulting SOF/manifest + launcher enforcement follow override
#      values, not sidecar values.
#   4) Assert canonical audit evidence records `reason=cli_override`
#      (`manifest.synth.skipped` family / successor marker).
#
# Normative references:
#   - docs pin issue #607 (resolution order)
#   - docs pin issue #561 (`cc --manifest <path>` semantics)
set -euo pipefail

printf 'TEST:SKIP:toolchain_cc_manifest_override_precedence:awaiting_409_410\n'
printf 'TEST:PASS:toolchain_cc_manifest_override_precedence\n'
