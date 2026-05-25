#!/usr/bin/env bash
# build/scripts/test_manifest_broker_role_enum.sh
#
# Issue #312: positive + negative coverage for the optional
# `capabilities.broker_role` enum (provider|consumer|none, default
# "none") added to manifests/schema/v0.json.
#
# Asserts:
#
#   1. The checked-in positive examples
#      (manifests/examples/helloapp.broker_provider.json and
#      manifests/examples/helloapp.broker_consumer.json) validate
#      against the schema and are reported as MANIFEST_VALIDATE:PASS
#      by the standard validator wrapper.
#   2. A synthesized manifest that sets `capabilities.broker_role` to
#      a value outside the enum (e.g. "everyone") is REJECTED by the
#      validator wrapper, with a deterministic MANIFEST_VALIDATE:(ERROR|FAIL)
#      marker on stderr, and the offending field name surfaces in the
#      error text. This is the regression that keeps a typo'd / drifted
#      enum from silently leaking back in.
#   3. The pre-existing `helloapp.json` (which omits `broker_role`
#      entirely) still validates, proving the field is additive and
#      backward-compatible (default = "none", today's behavior).
#
# Exit codes:
#   0  - all checks passed
#   1  - regression: validator accepted a bad enum, or rejected a good one
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_broker_role_enum

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"
POS_PROVIDER="$ROOT_DIR/manifests/examples/helloapp.broker_provider.json"
POS_CONSUMER="$ROOT_DIR/manifests/examples/helloapp.broker_consumer.json"
POS_OMITTED="$ROOT_DIR/manifests/examples/helloapp.json"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi
for f in "$POS_PROVIDER" "$POS_CONSUMER" "$POS_OMITTED"; do
  if [[ ! -r "$f" ]]; then
    echo "TEST:FAIL:harness_missing_positive_example:$f" >&2
    exit 78
  fi
done

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

# --- Positives: each must validate and emit MANIFEST_VALIDATE:PASS. ---
check_positive() {
  local label="$1"
  local path="$2"
  local out="$TMP/${label}.log"
  set +e
  bash "$WRAPPER" "$path" >"$out" 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -ne 0 ]]; then
    echo "TEST:FAIL:manifest_broker_role_enum:${label}_rejected" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
  if ! grep -q "MANIFEST_VALIDATE:PASS" "$out"; then
    echo "TEST:FAIL:manifest_broker_role_enum:${label}_missing_pass_marker" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
}

check_positive "provider_example" "$POS_PROVIDER"
check_positive "consumer_example" "$POS_CONSUMER"
check_positive "omitted_field_backcompat" "$POS_OMITTED"

# --- Negative: synthesize an out-of-enum value and assert rejection. ---
TAMPERED="$TMP/broker_role_bad.json"
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
    "broker_role": "everyone"
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
  echo "TEST:FAIL:manifest_broker_role_enum:bad_enum_accepted" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_broker_role_enum:missing_deterministic_marker" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -q "broker_role" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_broker_role_enum:field_name_not_in_error" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi

echo "TEST:PASS:manifest_broker_role_enum"
