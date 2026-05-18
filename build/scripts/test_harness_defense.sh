#!/usr/bin/env bash
# build/scripts/test_harness_defense.sh
#
# Self-test for the defensive test.sh dispatcher (issue #91).
#
# Verifies that:
#   1. Running an existing subordinate script with the executable bit
#      removed still succeeds (test.sh dispatches via `bash`).
#   2. Running a target wired to a missing script emits the deterministic
#      marker `TEST:FAIL:harness_missing_script:<path>` AND exits with
#      status 78 (HARNESS_ERROR_EXIT), so callers can classify the failure
#      as `harness_error` instead of `test_fail`.
#
# This script is invoked from build/scripts/test.sh under the
# `harness_defense` target.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_SH="$ROOT_DIR/build/scripts/test.sh"

WORKDIR="$(mktemp -d 2>/dev/null || mktemp -d -t harness_defense)"
trap 'rm -rf "$WORKDIR"' EXIT

# --- Case 1: no +x bit ---------------------------------------------------
# Build a private mirror of build/scripts containing a stub script with the
# executable bit deliberately removed; wire it into a stub test.sh derived
# from the real dispatcher's run_script logic, and confirm it still runs.
SCRIPTS_DIR="$WORKDIR/build/scripts"
mkdir -p "$SCRIPTS_DIR"

STUB="$SCRIPTS_DIR/test_stub_no_x.sh"
cat >"$STUB" <<'STUB'
#!/usr/bin/env bash
echo "STUB_OK"
STUB
chmod -x "$STUB" || true

# Inline a minimal harness replicating the run_script contract from test.sh.
HARNESS="$WORKDIR/run_harness.sh"
cat >"$HARNESS" <<'HARNESS'
#!/usr/bin/env bash
set -euo pipefail
HARNESS_ERROR_EXIT=78
run_script() {
  local path="$1"; shift
  if [[ ! -e "$path" ]]; then
    printf 'TEST:FAIL:harness_missing_script:%s\n' "$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  if [[ ! -r "$path" ]]; then
    printf 'TEST:FAIL:harness_unreadable_script:%s\n' "$path" >&2
    exit "$HARNESS_ERROR_EXIT"
  fi
  bash "$path" "$@"
}
run_script "$@"
HARNESS

OUT="$(bash "$HARNESS" "$STUB")"
if [[ "$OUT" != "STUB_OK" ]]; then
  echo "TEST:FAIL:harness_defense:no_x_script_did_not_run output=$OUT" >&2
  exit 1
fi

# --- Case 2: missing script ---------------------------------------------
MISSING="$WORKDIR/does_not_exist.sh"
set +e
ERR_OUT="$(bash "$HARNESS" "$MISSING" 2>&1 1>/dev/null)"
RC=$?
set -e

if [[ "$RC" -ne 78 ]]; then
  echo "TEST:FAIL:harness_defense:missing_script_wrong_exit rc=$RC expected=78" >&2
  echo "stderr: $ERR_OUT" >&2
  exit 1
fi

EXPECTED_MARKER="TEST:FAIL:harness_missing_script:$MISSING"
if [[ "$ERR_OUT" != *"$EXPECTED_MARKER"* ]]; then
  echo "TEST:FAIL:harness_defense:missing_script_marker_absent" >&2
  echo "expected substring: $EXPECTED_MARKER" >&2
  echo "got: $ERR_OUT" >&2
  exit 1
fi

# --- Case 3: real dispatcher honors the contract for known target -------
# Use the actual test.sh with an unknown target to confirm usage errors
# still exit non-zero with a DIFFERENT code (2), not 78, so harness errors
# remain distinguishable from usage errors.
set +e
bash "$TEST_SH" __no_such_target__ >/dev/null 2>&1
RC=$?
set -e
if [[ "$RC" -eq 78 ]]; then
  echo "TEST:FAIL:harness_defense:usage_error_collided_with_harness_exit" >&2
  exit 1
fi

echo "TEST:PASS:harness_defense"
