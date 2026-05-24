#!/usr/bin/env bash
# build/scripts/test_manifest_persistence_enum.sh
#
# Issue #285: positive + negative coverage for the optional
# `capabilities.persistence` enum (ephemeral|persistent, default
# ephemeral) added to manifests/schema/v0.json.
#
# Asserts:
#
#   1. The checked-in positive example (manifests/examples/helloapp.persistent.json)
#      validates against the schema and is reported as MANIFEST_VALIDATE:PASS
#      by the standard validator wrapper.
#   2. A synthesized manifest that sets `capabilities.persistence` to a
#      value outside the enum (e.g. "forever") is REJECTED by the
#      validator wrapper, with a deterministic MANIFEST_VALIDATE:(ERROR|FAIL)
#      marker on stderr, and the offending field name surfaces in the
#      error text. This is the regression that keeps a typo'd / drifted
#      enum from silently leaking back in.
#
# Exit codes:
#   0  - both checks passed
#   1  - regression: validator accepted a bad enum, or missed the good one
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_persistence_enum

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"
POSITIVE="$ROOT_DIR/manifests/examples/helloapp.persistent.json"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi
if [[ ! -r "$POSITIVE" ]]; then
  echo "TEST:FAIL:harness_missing_positive_example:$POSITIVE" >&2
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

# --- Positive: the checked-in persistent example must validate. ---
POS_OUT="$TMP/pos.log"
set +e
bash "$WRAPPER" "$POSITIVE" >"$POS_OUT" 2>&1
POS_RC=$?
set -e

if [[ "$POS_RC" -ne 0 ]]; then
  echo "TEST:FAIL:manifest_persistence_enum:positive_example_rejected" >&2
  sed 's/^/  | /' "$POS_OUT" >&2
  exit 1
fi
if ! grep -q "MANIFEST_VALIDATE:PASS" "$POS_OUT"; then
  echo "TEST:FAIL:manifest_persistence_enum:positive_example_missing_pass_marker" >&2
  sed 's/^/  | /' "$POS_OUT" >&2
  exit 1
fi

# --- Negative: synthesize an out-of-enum value and assert rejection. ---
TAMPERED="$TMP/persistence_bad.json"
cat >"$TAMPERED" <<'EOF'
{
  "manifest_version": 0,
  "os_abi_version": 0,
  "app": {
    "id": "helloapp",
    "version": "0.1.0",
    "subject_id": 2,
    "binary": "apps/helloapp.bin",
    "signer_key_id": "secureos-dev-key-1"
  },
  "capabilities": {
    "request": ["CAP_CONSOLE_WRITE"],
    "optional": [],
    "persistence": "forever"
  },
  "provides": [],
  "launcher": {
    "auto_grant_at_launch": ["CAP_CONSOLE_WRITE"],
    "require_user_confirm": []
  },
  "signature": {
    "algorithm": "ed25519",
    "signer_key_id": "secureos-dev-key-1",
    "signature_path": "apps/helloapp.bin.sig"
  }
}
EOF

NEG_OUT="$TMP/neg.log"
set +e
bash "$WRAPPER" "$TAMPERED" >"$NEG_OUT" 2>&1
NEG_RC=$?
set -e

if [[ "$NEG_RC" -eq 0 ]]; then
  echo "TEST:FAIL:manifest_persistence_enum:bad_enum_accepted" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_persistence_enum:missing_deterministic_marker" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -q "persistence" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_persistence_enum:field_name_not_in_error" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi

echo "TEST:PASS:manifest_persistence_enum"
