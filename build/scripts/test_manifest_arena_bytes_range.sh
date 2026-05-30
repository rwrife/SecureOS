#!/usr/bin/env bash
# build/scripts/test_manifest_arena_bytes_range.sh
#
# Issue #424: positive + negative coverage for the optional additive
# `runtime.arena_bytes` field (M7-TOOLCHAIN-001 schema sub-slice,
# refs #404 #421) added to manifests/schema/v0.json.
#
# Asserts:
#
#   1. The checked-in positive example
#      (manifests/examples/helloapp.runtime_arena.json) validates
#      against the schema and is reported as MANIFEST_VALIDATE:PASS
#      by the standard validator wrapper.
#   2. A checked-in negative fixture
#      (manifests/examples/invalid/helloapp.runtime_arena_invalid.json)
#      and synthesized in-test variants (below-minimum, negative,
#      string-typed) are REJECTED by the validator wrapper, with a
#      deterministic MANIFEST_VALIDATE:(ERROR|FAIL) marker on
#      stdout/stderr, and the offending field name surfaces in the
#      error text. This emits the `:negative_rejected` sub-marker
#      (parity with manifest_persistence_enum / manifest_broker_role_enum
#      / manifest_ownership_role_enum / manifest_owner_kind_enum).
#   3. The pre-existing `helloapp.json` (which omits the `runtime`
#      object entirely) still validates, proving the field is additive
#      and backward-compatible (default = kernel-default arena, today's
#      behavior). This emits the `:default_when_omitted` sub-marker.
#
# Exit codes:
#   0  - all checks passed
#   1  - regression: validator accepted a bad range, or rejected a good one
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:manifest_arena_bytes_range

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"
POS_RUNTIME="$ROOT_DIR/manifests/examples/helloapp.runtime_arena.json"
POS_OMITTED="$ROOT_DIR/manifests/examples/helloapp.json"
NEG_FIXTURE="$ROOT_DIR/manifests/examples/invalid/helloapp.runtime_arena_invalid.json"

if [[ ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi
for f in "$POS_RUNTIME" "$POS_OMITTED" "$NEG_FIXTURE"; do
  if [[ ! -r "$f" ]]; then
    echo "TEST:FAIL:harness_missing_fixture:$f" >&2
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
    echo "TEST:FAIL:manifest_arena_bytes_range:${label}_rejected" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
  if ! grep -q "MANIFEST_VALIDATE:PASS" "$out"; then
    echo "TEST:FAIL:manifest_arena_bytes_range:${label}_missing_pass_marker" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
}

check_positive "runtime_arena_example" "$POS_RUNTIME"
check_positive "omitted_field_backcompat" "$POS_OMITTED"

# Default-when-omitted sub-marker.
echo "TEST:PASS:manifest_arena_bytes_range:default_when_omitted"

# --- Negative (checked-in fixture): over-max value. ---
check_negative() {
  local label="$1"
  local path="$2"
  local out="$TMP/${label}.log"
  set +e
  bash "$WRAPPER" "$path" >"$out" 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "TEST:FAIL:manifest_arena_bytes_range:${label}_accepted" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
  if ! grep -Eq "MANIFEST_VALIDATE:(ERROR|FAIL)" "$out"; then
    echo "TEST:FAIL:manifest_arena_bytes_range:${label}_missing_deterministic_marker" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
  if ! grep -q "arena_bytes" "$out"; then
    echo "TEST:FAIL:manifest_arena_bytes_range:${label}_field_name_not_in_error" >&2
    sed 's/^/  | /' "$out" >&2
    exit 1
  fi
}

check_negative "over_max_fixture" "$NEG_FIXTURE"

# --- Synthesized negatives: below-min, negative integer, wrong type. ---
gen_tampered() {
  local path="$1"
  local arena_field="$2"
  cat >"$path" <<EOF
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
  "runtime": {
    "arena_bytes": ${arena_field}
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
}

BELOW_MIN="$TMP/arena_below_min.json"
gen_tampered "$BELOW_MIN" "1024"
check_negative "below_min_synth" "$BELOW_MIN"

NEG_INT="$TMP/arena_negative.json"
gen_tampered "$NEG_INT" "-1"
check_negative "negative_value_synth" "$NEG_INT"

WRONG_TYPE="$TMP/arena_wrong_type.json"
gen_tampered "$WRONG_TYPE" "\"big\""
check_negative "wrong_type_synth" "$WRONG_TYPE"

# Negative-path sub-marker (parity with peer enum tests).
echo "TEST:PASS:manifest_arena_bytes_range:negative_rejected"

echo "TEST:PASS:manifest_arena_bytes_range"
