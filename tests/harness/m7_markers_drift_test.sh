#!/usr/bin/env bash
# tests/harness/m7_markers_drift_test.sh
#
# Negative canary for the M7-TOOLCHAIN markers drift validator
# (issue #494).
#
# Why this exists:
#   build/scripts/validate_m7_markers.sh claims that a marker rename in
#   tests/m7_toolchain/markers.json which no longer matches the case-arm
#   wiring in build/scripts/test.sh + TEST_TARGETS in
#   build/scripts/validate_bundle.sh is a hard failure. This canary
#   proves that claim is real — not a silent no-op from a stale
#   dispatcher arm, a typo in the regex, or a parse path that walks off
#   the end of the JSON without consuming any rows. Mirrors the
#   discipline in #213 (validator harness self-test) and the
#   #234 / capability_registry_drift, #297 / abi_stamps_drift, and
#   #351 / sosh_contract_registry_drift canaries.
#
# Mechanics:
#   1. Build a throwaway repo skeleton containing:
#        - tools/validate_m7_markers.py (copied from this repo)
#        - tests/m7_toolchain/markers.json with TWO markers; one of
#          them deliberately renamed to a name the sandbox test.sh /
#          validate_bundle.sh do not know about.
#        - tests/m7_toolchain/<original>.sh stub (still emits the
#          original TEST:PASS line, so the rename in markers.json is
#          now an orphan against the stubs as well).
#        - build/scripts/test.sh + validate_bundle.sh shims that only
#          wire the ORIGINAL two marker names.
#   2. Run validate_m7_markers.py --allow-offline against that sandbox
#      and assert:
#        - exit code 1
#        - stderr contains the rename-side drift markers
#          (M7_MARKER:FAIL:<renamed>:missing_from_test_sh and
#           M7_MARKER:FAIL:<renamed>:missing_from_validate_bundle_targets)
#        - stderr contains the orphan-side drift markers for the
#          original name still wired in test.sh / TEST_TARGETS
#          (M7_MARKER:FAIL:<original>:orphan_test_sh_arm and
#           M7_MARKER:FAIL:<original>:orphan_validate_bundle_target)
#        - stderr contains the summary line
#          (M7_MARKER:FAIL:summary:<n>_failures)
#        - stdout still contains a PASS stub-check line for the
#          unrenamed second marker (proving the validator did walk all
#          markers and was not fail-fast on the first row).
#
# Contract:
#   On success: TEST:PASS:m7_markers_drift_canary, exit 0.
#   On failure: TEST:FAIL:m7_markers_drift_canary:<reason>, exit 1.

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMP_DIR'" EXIT

printf 'TEST:START:m7_markers_drift_canary\n'

SANDBOX="$TMP_DIR/repo"
mkdir -p "$SANDBOX/tools" \
         "$SANDBOX/tests/m7_toolchain" \
         "$SANDBOX/build/scripts"

cp "$ROOT_DIR/tools/validate_m7_markers.py" \
   "$SANDBOX/tools/validate_m7_markers.py" \
  || { printf 'TEST:FAIL:m7_markers_drift_canary:tool_copy_failed\n'; exit 1; }

# Sandbox markers.json — first marker has been renamed (drifted) away
# from the wiring in test.sh / validate_bundle.sh below; second marker
# is consistent and exists to prove the validator walks the full list.
cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "umbrella": 403,
  "markers": [
    {
      "name": "toolchain_renamed_marker",
      "gatingIssue": 409,
      "reason": "awaiting_409",
      "description": "drifted name not in test.sh / TEST_TARGETS"
    },
    {
      "name": "toolchain_compiles_hello_in_os",
      "gatingIssue": 409,
      "reason": "awaiting_409",
      "description": "still wired through every layer"
    }
  ]
}
EOF

# Stub script for the still-wired marker only. The renamed marker
# deliberately has no stub so the rename-side check also catches the
# missing_stub_script arm — but we assert only on the test.sh /
# TEST_TARGETS drift below to keep the canary focused on the
# wiring-contract failure modes #494 exists to catch.
cat > "$SANDBOX/tests/m7_toolchain/toolchain_compiles_hello_in_os.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'TEST:SKIP:toolchain_compiles_hello_in_os:awaiting_409\n'
printf 'TEST:PASS:toolchain_compiles_hello_in_os\n'
EOF
chmod +x "$SANDBOX/tests/m7_toolchain/toolchain_compiles_hello_in_os.sh"

# Sandbox test.sh that wires the ORIGINAL pair of names (so the
# rename in markers.json becomes a missing_from_test_sh drift, and the
# original survivor in test.sh becomes an orphan_test_sh_arm drift).
cat > "$SANDBOX/build/scripts/test.sh" <<'EOF'
#!/usr/bin/env bash
TEST_NAME="$1"
case "$TEST_NAME" in
  toolchain_compiles_hello_in_os|toolchain_runs_compiled_binary)
    bash "tests/m7_toolchain/${TEST_NAME}.sh"
    ;;
esac
EOF

# Sandbox validate_bundle.sh with a TEST_TARGETS block that mirrors
# the test.sh wiring above.
cat > "$SANDBOX/build/scripts/validate_bundle.sh" <<'EOF'
#!/usr/bin/env bash
TEST_TARGETS=(
    # M7-TOOLCHAIN sandbox (drift canary).
    toolchain_compiles_hello_in_os
    toolchain_runs_compiled_binary
)
EOF

PY="${PYTHON:-python3}"
OUT="$TMP_DIR/stdout"
ERR="$TMP_DIR/stderr"

set +e
"$PY" "$SANDBOX/tools/validate_m7_markers.py" \
  --root "$SANDBOX" --allow-offline \
  > "$OUT" 2> "$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_markers_drift_canary:unexpected_exit_code:%d\n' "$RC"
  printf '----- stdout -----\n' >&2; cat "$OUT" >&2
  printf '----- stderr -----\n' >&2; cat "$ERR" >&2
  exit 1
fi

# Rename-side drift: markers.json -> test.sh / validate_bundle.sh.
if ! grep -Fq 'M7_MARKER:FAIL:toolchain_renamed_marker:missing_from_test_sh' "$ERR"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:missing_from_test_sh_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi
if ! grep -Fq 'M7_MARKER:FAIL:toolchain_renamed_marker:missing_from_validate_bundle_targets' "$ERR"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:missing_from_validate_bundle_targets_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

# Orphan-side drift: name still wired in test.sh / TEST_TARGETS but
# absent from markers.json.
if ! grep -Fq 'M7_MARKER:FAIL:toolchain_runs_compiled_binary:orphan_test_sh_arm' "$ERR"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:orphan_test_sh_arm_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi
if ! grep -Fq 'M7_MARKER:FAIL:toolchain_runs_compiled_binary:orphan_validate_bundle_target' "$ERR"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:orphan_validate_bundle_target_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

# Summary marker proves we reached the end of main() instead of
# crashing on a parse path.
if ! grep -Eq '^M7_MARKER:FAIL:summary:[0-9]+_failures$' "$ERR"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:summary_marker_absent\n'
  cat "$ERR" >&2
  exit 1
fi

# Proof of full walk: the survivor marker emitted its stub-check PASS
# line, so the validator did not fail-fast on the first row.
if ! grep -Fq 'M7_MARKER:PASS:toolchain_compiles_hello_in_os:stub_pass_marker_present' "$OUT"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:full_walk_pass_absent\n'
  cat "$OUT" >&2
  exit 1
fi

printf 'TEST:PASS:m7_markers_drift_canary\n'
exit 0
