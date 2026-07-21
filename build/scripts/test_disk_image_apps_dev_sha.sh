#!/usr/bin/env bash
# Host drift gate for /apps/dev source SHA pin (issue #606).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_disk_image_apps_dev_sha.sh"
CANONICAL_SKIP="SKIP:#531,#548"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
OUT="$TMP/apps_dev_sha.log"

set +e
bash "$WRAPPER" >"$OUT" 2>&1
RC=$?
set -e

cat "$OUT"

if [[ "$RC" -ne 0 ]]; then
  echo "TEST:FAIL:apps_dev_sha:validator_failed" >&2
  exit 1
fi

if grep -qx "$CANONICAL_SKIP" "$OUT"; then
  echo "TEST:SKIP:apps_dev_sha:$CANONICAL_SKIP"
fi

grep -q "APPS_DEV_SHA:PASS:summary:" "$OUT"

echo "TEST:PASS:apps_dev_sha"
