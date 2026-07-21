#!/usr/bin/env bash
# Host drift gate for SOF wire-format constants pin (issue #547).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_sof_format_constants.sh"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/sof_format_constants.log"

set +e
bash "$WRAPPER" >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:sof_format_constants:validator_failed" >&2
  exit 1
fi

grep -q "SOF_FORMAT_CONSTANTS:PASS:summary:" "$OUT"

echo "TEST:PASS:sof_format_constants"
