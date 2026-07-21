#!/usr/bin/env bash
# build/scripts/test_manifestgen_schema_valid.sh
#
# Issue #588 schema-validity gate for libmanifestgen output. Exercises three
# representative synthesis scenarios and validates each emitted manifest
# against manifests/schema/v0.json via build/scripts/validate_manifests.sh.
#
# Scenarios pinned:
#   1) minimal           -> no caps beyond defaults, owner=internal
#   2) explicit_runtime  -> owner=internal with runtime.arena_bytes override
#   3) local_owner       -> owner=local (in-OS compile provenance)
#
# On validation failure, prints:
#   - scenario label
#   - validator output (schema-rule diagnostics)
#   - unified diff to a closest valid reference shape

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

DRIVER="$OUT_DIR/manifestgen_schema_valid_driver"
VALIDATE="$ROOT_DIR/build/scripts/validate_manifests.sh"

mkdir -p "$OUT_DIR"

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/manifest_default_synthesise_test.c" \
  -o "$DRIVER"

if [[ ! -r "$VALIDATE" ]]; then
  echo "TEST:FAIL:manifestgen_schema_valid:validator_missing" >&2
  exit 78
fi

print_shape_diff() {
  local expected="$1"
  local actual="$2"
  if [[ ! -f "$expected" ]]; then
    echo "  | (no reference file for diff: $expected)" >&2
    return 0
  fi
  python3 - "$expected" "$actual" <<'PY' || true
import difflib
import json
import sys
from pathlib import Path

exp_path = Path(sys.argv[1])
act_path = Path(sys.argv[2])

try:
    exp_obj = json.loads(exp_path.read_text(encoding='utf-8'))
    act_obj = json.loads(act_path.read_text(encoding='utf-8'))
except Exception as exc:
    print(f"  | diff unavailable (json parse/read error): {exc}")
    raise SystemExit(0)

exp_text = json.dumps(exp_obj, indent=2, sort_keys=True) + "\n"
act_text = json.dumps(act_obj, indent=2, sort_keys=True) + "\n"
for line in difflib.unified_diff(
    exp_text.splitlines(keepends=True),
    act_text.splitlines(keepends=True),
    fromfile=f"expected:{exp_path}",
    tofile=f"actual:{act_path}",
):
    sys.stdout.write("  | " + line)
PY
}

validate_case() {
  local label="$1"
  local owner="$2"
  local runtime="$3"
  local reference="$4"
  local manifest_path="$TMP_DIR/${label}.manifest.json"
  local vout="$TMP_DIR/${label}.validate.log"

  "$DRIVER" "$manifest_path" "$owner" "$runtime"

  if [[ ! -s "$manifest_path" ]]; then
    echo "TEST:FAIL:manifestgen_schema_valid:${label}:empty_output" >&2
    exit 1
  fi

  set +e
  bash "$VALIDATE" "$manifest_path" >"$vout" 2>&1
  local rc=$?
  set -e

  if [[ "$rc" -ne 0 ]]; then
    echo "TEST:FAIL:manifestgen_schema_valid:${label}:schema_reject" >&2
    sed 's/^/  | /' "$vout" >&2
    print_shape_diff "$reference" "$manifest_path" >&2
    exit 1
  fi

  if ! grep -Eq "MANIFEST_VALIDATE:(PASS|SUMMARY)" "$vout"; then
    echo "TEST:FAIL:manifestgen_schema_valid:${label}:missing_pass_marker" >&2
    sed 's/^/  | /' "$vout" >&2
    exit 1
  fi

  echo "TEST:PASS:manifestgen_schema_valid:scenario:${label}"
}

validate_case "minimal" "internal" "0" \
  "$ROOT_DIR/tests/manifestgen/schema_valid_refs/minimal.json"
validate_case "explicit_runtime" "internal" "131072" \
  "$ROOT_DIR/tests/manifestgen/schema_valid_refs/explicit_runtime.json"
validate_case "local_owner" "local" "65536" \
  "$ROOT_DIR/tests/manifestgen/schema_valid_refs/local_owner.json"

echo "TEST:PASS:manifestgen_schema_valid"
