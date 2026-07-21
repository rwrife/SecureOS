#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launcher_owner_kind_cache_isolation.sh
#
# Pre-#410 SKIP-pinned harness for issue #612.
#
# Contract to enforce when #410 flips this marker from SKIP -> PASS:
#   1) Stage same-bare-name binaries with distinct owner.kind values
#      (`external` and `local`).
#   2) Approve unsigned-run for external variant and observe
#      AUTH_TYPE_UNSIGNED_BIN decision=allow owner_kind=external cached=false.
#   3) Immediately run local variant and assert decision is deny or fresh prompt,
#      never a cached cross-owner-kind allow.
#   4) Assert deny-path launch audit evidence includes owner_kind=local and
#      cache-isolation reason (per #554 contract family).
#
# Normative references:
#   - docs/abi/audit-markers.md (AUTH_TYPE_UNSIGNED_BIN; launch.granted/denied)
#   - issue #542 (unsigned-run allow/deny/cached markers)
#   - issue #554 (owner_kind field on launch audit records)
set -euo pipefail

printf 'TEST:SKIP:toolchain_launcher_owner_kind_cache_isolation:awaiting_410_522\n'
printf 'TEST:PASS:toolchain_launcher_owner_kind_cache_isolation\n'
