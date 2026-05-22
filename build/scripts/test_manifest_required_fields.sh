#!/usr/bin/env bash
# build/scripts/test_manifest_required_fields.sh
#
# Issue #226: negative regression test for the build-pipeline manifest
# gate. Synthesizes a tampered manifest in a tempdir (dropping the
# required `os_abi_version` field), runs the validator wrapper against
# it, and asserts:
#
#   1. The validator exits non-zero.
#   2. Stderr contains a deterministic `MANIFEST_VALIDATE:ERROR` or
#      `MANIFEST_VALIDATE:FAIL` marker so CI log scrapers can detect
#      it without false positives.
#
# Exit codes:
#   0  - regression caught (validator correctly rejected the tampered
#        manifest)
#   1  - regression LEAKED (validator accepted the tampered manifest
#        or emitted no deterministic marker)
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_required_fields

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi

PY="${PYTHON:-python3}"
if ! command -v "$PY" >/dev/null 2>&1; then
  echo "TEST:FAIL:harness_missing_python" >&2
  exit 78
fi
if ! "$PY" -c "import jsonschema" >/dev/null 2>&1; then
  echo "TEST:FAIL:harness_missing_jsonschema" >&2
  exit 78
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Synthesize a manifest that is JSON-valid but schema-invalid:
# `os_abi_version` is required by manifests/schema/v0.json and we
# intentionally omit it.
TAMPERED="$TMP/tampered.json"
cat >"$TAMPERED" <<'EOF'
{
  "manifest_version": 0,
  "app": {
    "id": "tampered",
    "version": "0.0.1",
    "subject_id": 2,
    "binary": "apps/tampered.bin"
  },
  "capabilities": {
    "request": []
  }
}
EOF

OUT="$TMP/out.log"
set +e
bash "$WRAPPER" "$TAMPERED" >"$OUT" 2>&1
RC=$?
set -e

if [[ "$RC" -eq 0 ]]; then
  echo "TEST:FAIL:manifest_required_fields:validator_returned_zero_on_tampered" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$OUT"; then
  echo "TEST:FAIL:manifest_required_fields:missing_deterministic_marker" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

# Specifically: the omission of `os_abi_version` must surface in the
# error message. Be permissive about exact wording (jsonschema text
# may vary across versions) but require the field name.
if ! grep -q "os_abi_version" "$OUT"; then
  echo "TEST:FAIL:manifest_required_fields:os_abi_version_not_mentioned" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

echo "TEST:PASS:manifest_required_fields"
