#!/usr/bin/env bash
# Host drift gate for dev/building.txt staging + phase-label parity (issue #618).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_dev_building_txt.sh"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/dev_building_txt_drift.log"

set +e
bash "$WRAPPER" >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:dev_building_txt_drift:validator_failed" >&2
  exit 1
fi

grep -q "DEV_BUILDING_TXT:PASS:summary:" "$OUT"

echo "TEST:PASS:dev_building_txt_drift"
