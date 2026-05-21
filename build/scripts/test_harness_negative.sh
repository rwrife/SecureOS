#!/usr/bin/env bash
# test_harness_negative.sh
#
# Harness self-test (BUILD_ROADMAP §3.4 / §8 item 9, issue #173).
#
# What this proves
# ----------------
# The existing `hello_boot_negative` target verifies that a deliberately
# failing boot fixture is *detected* by the harness (wrapper inverts the
# exit code: fixture exits fail, harness reports pass). That alone is not
# enough to falsify a silent-skip bug, because we never observe what the
# harness does when it *should* surface a failure.
#
# This self-test runs the same `hello_boot_fail` fixture but forces the
# wrapper to expect a *passing* run via SECUREOS_FORCE_EXPECTED_STATUS=pass.
# The wrapper MUST then:
#   1. exit non-zero, and
#   2. write a log that contains the `TEST:FAIL:hello_boot_fail:` marker.
#
# If either check fails, the harness is silently swallowing failures and
# every previous "green" run is unfalsifiable.
#
# Used by
# -------
#   build/scripts/test.sh harness_negative
#   build/scripts/validate_bundle.sh   (TEST_TARGETS)
#
# Independent of the M2/M3/M4 red-CI chain (#101/#106/#133): only touches
# the bootloader fixture + run_qemu.sh harness.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

echo "TEST:START:harness_negative"

# Build the intentionally failing boot sector fixture.
"$ROOT_DIR/build/scripts/test_boot_sector_fail.sh" >/dev/null

LOG_FILE="$ROOT_DIR/artifacts/qemu/hello_boot_fail.log"
META_FILE="$ROOT_DIR/artifacts/qemu/hello_boot_fail.meta.json"
mkdir -p "$(dirname "$LOG_FILE")"

# Run the wrapper with inverted expectations. We expect it to FAIL (non-zero).
set +e
SECUREOS_FORCE_EXPECTED_STATUS=pass \
  "$ROOT_DIR/build/scripts/run_qemu.sh" --test hello_boot_fail >/dev/null 2>&1
HARNESS_RC=$?
set -e

if [[ "$HARNESS_RC" -eq 0 ]]; then
  echo "TEST:FAIL:harness_negative:wrapper-returned-zero-on-known-failing-fixture"
  exit 1
fi

if [[ ! -f "$LOG_FILE" ]]; then
  echo "TEST:FAIL:harness_negative:missing-log:$LOG_FILE"
  exit 1
fi

if ! grep -q "TEST:FAIL:hello_boot_fail:" "$LOG_FILE"; then
  echo "TEST:FAIL:harness_negative:missing-fail-marker-in-log"
  exit 1
fi

# Meta JSON should also record the inverted-expectation failure so downstream
# bundle inspection can see it.
if [[ -f "$META_FILE" ]] && command -v python3 >/dev/null 2>&1; then
  if ! python3 - "$META_FILE" <<'PY'
import json, sys
meta = json.loads(open(sys.argv[1]).read())
ok = (
    meta.get("expectedStatus") == "pass"
    and meta.get("status") == "fail"
    and meta.get("markers", {}).get("fail") is True
)
sys.exit(0 if ok else 1)
PY
  then
    echo "TEST:FAIL:harness_negative:meta-json-did-not-record-inverted-failure"
    exit 1
  fi
fi

echo "TEST:PASS:harness_negative:wrapper_rc=${HARNESS_RC}"
