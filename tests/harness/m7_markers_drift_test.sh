#!/usr/bin/env bash
# tests/harness/m7_markers_drift_test.sh
#
# Negative canary for the M7-markers drift validator (issue #494,
# umbrella #403). Same shape as #213 (validator harness self-test),
# #234 / capability_registry_drift, #297 / abi_stamps_drift, and
# #351 / sosh_contract_registry_drift_test.sh.
#
# Why this exists:
#   build/scripts/validate_m7_markers.sh claims that a marker rename
#   in tests/m7_toolchain/markers.json that is not paired with the
#   corresponding rename in build/scripts/test.sh, build/scripts/
#   validate_bundle.sh, or the per-marker tests/m7_toolchain/*.sh
#   stub is a hard failure. This canary proves that claim is real —
#   not a silent no-op from a stale dispatcher arm, a typo in the
#   regex, or a path that walks the marker list without consuming
#   any rows. Same discipline as the capability_registry_drift /
#   abi_stamps_drift / sosh_contract_registry_drift peers.
#
# Mechanics:
#   1. Build a throwaway sandbox repo skeleton mirroring the real
#      tree shape (tools/, tests/m7_toolchain/, build/scripts/).
#   2. Copy the real tools/validate_m7_markers.py into the sandbox.
#   3. Write a markers.json that names ONE marker
#      (`toolchain_compiles_hello_in_os`), then mutate it to
#      `toolchain_RENAMED` — but leave test.sh, validate_bundle.sh,
#      and the per-marker stub still spelling the original. The
#      validator must catch:
#        - missing_test_sh_arm:toolchain_RENAMED
#        - orphan_test_sh_arm:toolchain_compiles_hello_in_os
#        - missing_test_targets:toolchain_RENAMED
#        - orphan_test_targets:toolchain_compiles_hello_in_os
#        - script_missing:toolchain_RENAMED:...
#   4. Bonus second canary: restore the marker name, but mutate the
#      `reason` to `awaiting_99999` so the paired
#      reason-vs-gatingIssue arm fires:
#        - reason_issue_mismatch:toolchain_compiles_hello_in_os:
#          reason=awaiting_99999:expected=awaiting_409
#      This proves the second check arm is wired (mirrors the
#      multi-check coverage of capability_registry_drift_test.sh and
#      sosh_contract_registry_drift_test.sh).
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
mkdir -p \
  "$SANDBOX/tools" \
  "$SANDBOX/tests/m7_toolchain" \
  "$SANDBOX/build/scripts"

cp "$ROOT_DIR/tools/validate_m7_markers.py" \
   "$SANDBOX/tools/validate_m7_markers.py" \
  || { printf 'TEST:FAIL:m7_markers_drift_canary:tool_copy_failed\n'; exit 1; }

# A minimal test.sh stub that names exactly one toolchain_* case arm
# (the real marker spelling).
cat > "$SANDBOX/build/scripts/test.sh" <<'EOF'
#!/usr/bin/env bash
# sandbox test.sh — only the case arm matters for the validator
case "${1:-}" in
  toolchain_compiles_hello_in_os)
    bash tests/m7_toolchain/toolchain_compiles_hello_in_os.sh
    ;;
esac
EOF

# A minimal validate_bundle.sh stub with a TEST_TARGETS array
# containing the same single toolchain_* token.
cat > "$SANDBOX/build/scripts/validate_bundle.sh" <<'EOF'
#!/usr/bin/env bash
TEST_TARGETS=(
    toolchain_compiles_hello_in_os
)
EOF

# Per-marker stub emits BOTH the canonical SKIP and the PASS rollup
# under the real marker name.
cat > "$SANDBOX/tests/m7_toolchain/toolchain_compiles_hello_in_os.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'TEST:SKIP:toolchain_compiles_hello_in_os:awaiting_409\n'
printf 'TEST:PASS:toolchain_compiles_hello_in_os\n'
EOF

# ---- Canary 1: marker rename in json without paired renames ----
cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "name": "toolchain_RENAMED",
      "gatingIssue": 409,
      "reason": "awaiting_409",
      "description": "sandbox: deliberately drifted from test.sh / TEST_TARGETS / stub"
    }
  ]
}
EOF

OUT="$TMP_DIR/out1"
ERR="$TMP_DIR/err1"
set +e
python3 "$SANDBOX/tools/validate_m7_markers.py" \
  --root "$SANDBOX" --allow-offline > "$OUT" 2> "$ERR"
RC=$?
set -e

if [[ "$RC" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_markers_drift_canary:rename_unexpected_exit_code:%d\n' "$RC"
  printf '----- stdout -----\n' >&2; cat "$OUT" >&2
  printf '----- stderr -----\n' >&2; cat "$ERR" >&2
  exit 1
fi

for needle in \
  'M7_MARKER:FAIL:missing_test_sh_arm:toolchain_RENAMED' \
  'M7_MARKER:FAIL:orphan_test_sh_arm:toolchain_compiles_hello_in_os' \
  'M7_MARKER:FAIL:missing_test_targets:toolchain_RENAMED' \
  'M7_MARKER:FAIL:orphan_test_targets:toolchain_compiles_hello_in_os' \
  'M7_MARKER:FAIL:script_missing:toolchain_RENAMED'
do
  if ! grep -Fq "$needle" "$ERR"; then
    printf 'TEST:FAIL:m7_markers_drift_canary:missing_marker:%s\n' "$needle"
    cat "$ERR" >&2
    exit 1
  fi
done

# ---- Canary 2: reason drifts from gatingIssue ----
cat > "$SANDBOX/tests/m7_toolchain/markers.json" <<'EOF'
{
  "schemaVersion": 1,
  "markers": [
    {
      "name": "toolchain_compiles_hello_in_os",
      "gatingIssue": 409,
      "reason": "awaiting_99999",
      "description": "sandbox: reason intentionally drifted from gatingIssue"
    }
  ]
}
EOF

OUT2="$TMP_DIR/out2"
ERR2="$TMP_DIR/err2"
set +e
python3 "$SANDBOX/tools/validate_m7_markers.py" \
  --root "$SANDBOX" --allow-offline > "$OUT2" 2> "$ERR2"
RC2=$?
set -e

if [[ "$RC2" -ne 1 ]]; then
  printf 'TEST:FAIL:m7_markers_drift_canary:reason_unexpected_exit_code:%d\n' "$RC2"
  printf '----- stdout -----\n' >&2; cat "$OUT2" >&2
  printf '----- stderr -----\n' >&2; cat "$ERR2" >&2
  exit 1
fi

if ! grep -Fq \
  'M7_MARKER:FAIL:reason_issue_mismatch:toolchain_compiles_hello_in_os:reason=awaiting_99999:expected=awaiting_409' \
  "$ERR2"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:reason_mismatch_marker_absent\n'
  cat "$ERR2" >&2
  exit 1
fi

# The script SKIP-line check should also fire because the per-marker
# stub still emits `awaiting_409` while json claims `awaiting_99999`.
if ! grep -Fq \
  'M7_MARKER:FAIL:skip_reason_mismatch:toolchain_compiles_hello_in_os:script=awaiting_409:json=awaiting_99999' \
  "$ERR2"; then
  printf 'TEST:FAIL:m7_markers_drift_canary:skip_reason_marker_absent\n'
  cat "$ERR2" >&2
  exit 1
fi

printf 'TEST:PASS:m7_markers_drift_canary\n'
exit 0
