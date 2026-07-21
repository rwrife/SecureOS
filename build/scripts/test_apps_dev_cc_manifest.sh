#!/usr/bin/env bash
# Host drift gate for canonical /apps/dev/cc manifest pin (issue #573).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_apps_dev_cc_manifest.sh"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/apps_dev_cc_manifest.log"

set +e
bash "$WRAPPER" >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:apps_dev_cc_manifest:validator_failed" >&2
  exit 1
fi

if grep -q "CC_MANIFEST:SKIP:apps_dev_cc:awaiting_540" "$OUT"; then
  echo "TEST:SKIP:apps_dev_cc_manifest:awaiting_540"
  echo "TEST:PASS:apps_dev_cc_manifest"
  exit 0
fi

grep -q "CC_MANIFEST:PASS:pinned_shape" "$OUT"
grep -q "CC_MANIFEST:PASS:pinned_vs_staged" "$OUT"

echo "TEST:PASS:apps_dev_cc_manifest"
