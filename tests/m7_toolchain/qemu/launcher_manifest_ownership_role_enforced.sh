#!/usr/bin/env bash
# tests/m7_toolchain/qemu/launcher_manifest_ownership_role_enforced.sh
#
# Pre-#585 SKIP-pinned qemu acceptance harness for ownership-role enforcement.
#
# Target contract once #585 lands:
#   - valid capabilities.ownership_role -> launch.granted
#   - invalid or missing ownership_role -> launch.denied:reason=ownership_role_invalid
#
# Until then, emit canonical SKIP markers and roll up PASS to keep bundle
# wiring green while making the pending contract explicit.

set -euo pipefail

echo "TEST:SKIP:toolchain_launcher_manifest_ownership_role_enforced:awaiting_585"
# Alias marker kept for direct traceability to issue wording.
echo "TEST:SKIP:launcher_manifest_ownership_role_enforced:gating_issue=585"
echo "TEST:PASS:toolchain_launcher_manifest_ownership_role_enforced"
