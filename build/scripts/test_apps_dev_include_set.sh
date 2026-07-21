#!/usr/bin/env bash
# Host drift gate for canonical /apps/dev/include header-set pin (issue #615).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_apps_dev_include_set.sh"
CANONICAL_SKIP="SKIP:#613"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/apps_dev_include_set.log"

set +e
bash "$WRAPPER" >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:apps_dev_include_set:validator_failed" >&2
  exit 1
fi

if grep -qx "$CANONICAL_SKIP" "$OUT"; then
  echo "TEST:SKIP:apps_dev_include_set:$CANONICAL_SKIP"
fi

grep -q "APPS_DEV_INCLUDE_SET:PASS:summary:" "$OUT"

echo "TEST:PASS:apps_dev_include_set"
