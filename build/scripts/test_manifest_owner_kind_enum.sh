#!/usr/bin/env bash
# build/scripts/test_manifest_owner_kind_enum.sh
#
# Issue #396 (M6-SDK-003, schema sub-slice): positive + negative
# coverage for the optional `owner.kind` enum (internal|external,
# default "internal") added to manifests/schema/v0.json per
# plans/2026-05-15-public-sdk-external-app-template.md
# §"Manifest Schema (additions)" and BUILD_ROADMAP §5.6.
#
# This is the schema-only / additive half of M6-SDK-003. The
# os-cc / os-pack / os-run wrappers and the
# `sdk_external_build_isolation` acceptance test are intentionally
# out of scope here; they are gated on the maintainer A/B decision
# tracked in the body of #396.
#
# Asserts:
#
#   1. The checked-in positive examples
#      (manifests/examples/helloapp.owner_external.json and
#      manifests/examples/helloapp.owner_internal.json) validate
#      against the schema and are reported as MANIFEST_VALIDATE:PASS
#      by the standard validator wrapper.
#   2. A checked-in negative fixture
#      (manifests/examples/invalid/helloapp.owner_kind_invalid.json)
#      and a synthesized in-test variant that set `owner.kind` to a
#      value outside the enum (e.g. "vendor", "everyone") are
#      REJECTED by the validator wrapper, with a deterministic
#      MANIFEST_VALIDATE:(ERROR|FAIL) marker on stderr/stdout, and
#      the offending field path surfaces in the error text. This is
#      the regression that keeps a typo'd / drifted enum from
#      silently leaking back in. This path also emits the
#      `:negative_rejected` sub-marker (parity with §5.3
#      manifest_persistence_enum, §5.4 manifest_broker_role_enum,
#      and §5.5 manifest_ownership_role_enum).
#   3. The pre-existing `helloapp.json` (which omits the `owner`
#      object entirely) still validates, proving the field is
#      additive and backward-compatible (default = "internal",
#      today's behavior). This path also emits the
#      `:default_when_omitted` sub-marker (parity with §5.3 /
#      §5.4 / §5.5).
#
# Exit codes:
#   0  - all checks passed
#   1  - regression: validator accepted a bad enum, or rejected a good one
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_owner_kind_enum

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"
POS_EXTERNAL="$ROOT_DIR/manifests/examples/helloapp.owner_external.json"
POS_INTERNAL="$ROOT_DIR/manifests/examples/helloapp.owner_internal.json"
POS_LOCAL="$ROOT_DIR/manifests/examples/helloapp.owner_kind_local.json"
POS_OMITTED="$ROOT_DIR/manifests/examples/helloapp.json"
NEG_FIXTURE="$ROOT_DIR/manifests/examples/invalid/helloapp.owner_kind_invalid.json"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi
for f in "$POS_EXTERNAL" "$POS_INTERNAL" "$POS_LOCAL" "$POS_OMITTED" "$NEG_FIXTURE"; do
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
    echo "TEST:FAIL:manifest_owner_kind_enum:${label}_rejected" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
  if ! grep -q "MANIFEST_VALIDATE:PASS" "$out"; then
    echo "TEST:FAIL:manifest_owner_kind_enum:${label}_missing_pass_marker" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
}

check_positive "external_example" "$POS_EXTERNAL"
check_positive "internal_example" "$POS_INTERNAL"
check_positive "local_example" "$POS_LOCAL"
check_positive "omitted_field_backcompat" "$POS_OMITTED"

# Spelled-out positive sub-marker for the M7-TOOLCHAIN-006 'local'
# enumerator (issue #522, refs #409 / #410); parity with the
# `:default_when_omitted` / `:negative_rejected` sub-markers.
echo "TEST:PASS:manifest_owner_kind_enum:local_accepted"

# --- Default-when-omitted sub-marker (parity with §5.3/§5.4/§5.5). ---
# The omitted-field case above already passed; explicitly emit the
# spelled-out sub-marker so consumers / dashboards can match the same
# token shape as manifest_persistence_enum / manifest_broker_role_enum /
# manifest_ownership_role_enum.
echo "TEST:PASS:manifest_owner_kind_enum:default_when_omitted"

# --- Negative (checked-in fixture): assert rejection. ---
NEG_FIX_OUT="$TMP/neg_fixture.log"
set +e
bash "$WRAPPER" "$NEG_FIXTURE" >"$NEG_FIX_OUT" 2>&1
NEG_FIX_RC=$?
set -e

if [[ "$NEG_FIX_RC" -eq 0 ]]; then
  echo "TEST:FAIL:manifest_owner_kind_enum:negative_fixture_accepted" >&2
  sed 's/^/  | /' "$NEG_FIX_OUT" >&2
  exit 1
fi
if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$NEG_FIX_OUT"; then
  echo "TEST:FAIL:manifest_owner_kind_enum:negative_fixture_missing_deterministic_marker" >&2
  sed 's/^/  | /' "$NEG_FIX_OUT" >&2
  exit 1
fi
# Either "owner" or "kind" must appear in the error message — the
# field path "/owner/kind" surfaces from the jsonschema error.
if ! grep -Eq "owner|kind" "$NEG_FIX_OUT"; then
  echo "TEST:FAIL:manifest_owner_kind_enum:negative_fixture_field_name_not_in_error" >&2
  sed 's/^/  | /' "$NEG_FIX_OUT" >&2
  exit 1
fi

# --- Negative (synthesized in-test): assert rejection. ---
TAMPERED="$TMP/owner_kind_bad.json"
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
    "optional": []
  },
  "owner": {
    "kind": "everyone"
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
  echo "TEST:FAIL:manifest_owner_kind_enum:bad_enum_accepted" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_owner_kind_enum:missing_deterministic_marker" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi
if ! grep -Eq "owner|kind" "$NEG_OUT"; then
  echo "TEST:FAIL:manifest_owner_kind_enum:field_name_not_in_error" >&2
  sed 's/^/  | /' "$NEG_OUT" >&2
  exit 1
fi

# Negative-path sub-marker (parity with §5.3/§5.4/§5.5).
echo "TEST:PASS:manifest_owner_kind_enum:negative_rejected"

# --- Additional negatives for the 'local' enumerator added by #522.
# Assert that case-variant ('LOCAL') and near-miss ('local-shipped')
# values are still rejected — the enum is case-sensitive and exact,
# matching the precedent set by 'external' / 'internal'.
for BAD_VAL in "LOCAL" "local-shipped"; do
  BAD_PATH="$TMP/owner_kind_${BAD_VAL//\//_}.json"
  cat >"$BAD_PATH" <<EOF
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
    "optional": []
  },
  "owner": {
    "kind": "${BAD_VAL}"
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
  LOCAL_NEG_OUT="$TMP/local_neg_${BAD_VAL//\//_}.log"
  set +e
  bash "$WRAPPER" "$BAD_PATH" >"$LOCAL_NEG_OUT" 2>&1
  LOCAL_NEG_RC=$?
  set -e
  if [[ "$LOCAL_NEG_RC" -eq 0 ]]; then
    echo "TEST:FAIL:manifest_owner_kind_enum:local_near_miss_accepted:${BAD_VAL}" >&2
    sed 's/^/  | /' "$LOCAL_NEG_OUT" >&2
    exit 1
  fi
  if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$LOCAL_NEG_OUT"; then
    echo "TEST:FAIL:manifest_owner_kind_enum:local_near_miss_missing_marker:${BAD_VAL}" >&2
    sed 's/^/  | /' "$LOCAL_NEG_OUT" >&2
    exit 1
  fi
  if ! grep -Eq "owner|kind" "$LOCAL_NEG_OUT"; then
    echo "TEST:FAIL:manifest_owner_kind_enum:local_near_miss_field_name_not_in_error:${BAD_VAL}" >&2
    sed 's/^/  | /' "$LOCAL_NEG_OUT" >&2
    exit 1
  fi
done
echo "TEST:PASS:manifest_owner_kind_enum:local_near_miss_rejected"

echo "TEST:PASS:manifest_owner_kind_enum"
