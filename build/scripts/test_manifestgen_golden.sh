#!/usr/bin/env bash
# build/scripts/test_manifestgen_golden.sh
#
# Issue #577: byte-identical golden-corpus gate for libmanifestgen output.
#
# This host gate compiles the existing manifest synthesiser test driver
# (tests/manifest_default_synthesise_test.c in driver mode), executes a small
# corpus of canonical fixtures from tests/manifestgen_golden/, and enforces:
#   1) expected process return code per fixture
#   2) byte-identical output for success fixtures
#   3) schema-valid output via validate_manifests.sh for success fixtures
#   4) deterministic failure marker substring + no non-empty output on
#      negative fixtures
#
# Markers:
#   TEST:PASS:manifestgen_golden:<fixture_name>
#   TEST:PASS:manifestgen_golden

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
FIXTURE_DIR="$ROOT_DIR/tests/manifestgen_golden"
BIN="$OUT_DIR/manifestgen_golden_driver"
VALIDATE="$ROOT_DIR/build/scripts/validate_manifests.sh"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$OUT_DIR"

if [[ ! -d "$FIXTURE_DIR" ]]; then
  echo "TEST:FAIL:manifestgen_golden:fixture_dir_missing" >&2
  exit 78
fi

if [[ ! -x "$VALIDATE" && ! -r "$VALIDATE" ]]; then
  echo "TEST:FAIL:manifestgen_golden:validator_missing" >&2
  exit 78
fi

cc -std=c11 -Wall -Wextra -Werror \
  "$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c" \
  "$ROOT_DIR/tests/manifest_default_synthesise_test.c" \
  -o "$BIN"

ran_any=0
ran_negative=0

for fixture in "$FIXTURE_DIR"/*.fixture; do
  [[ -e "$fixture" ]] || continue

  ran_any=1
  unset FIXTURE_NAME OWNER_KIND RUNTIME_ARENA_BYTES EXPECT_RC EXPECT_OUTPUT EXPECT_VALIDATE_SCHEMA EXPECT_STDERR_SUBSTR
  # shellcheck disable=SC1090
  source "$fixture"

  if [[ -z "${FIXTURE_NAME:-}" || -z "${OWNER_KIND:-}" || -z "${EXPECT_RC:-}" ]]; then
    echo "TEST:FAIL:manifestgen_golden:fixture_missing_required_fields:$(basename "$fixture")" >&2
    exit 1
  fi

  if [[ -z "${RUNTIME_ARENA_BYTES:-}" ]]; then
    RUNTIME_ARENA_BYTES=0
  fi

  if [[ -z "${EXPECT_VALIDATE_SCHEMA:-}" ]]; then
    EXPECT_VALIDATE_SCHEMA=0
  fi

  out_path="$TMP_DIR/${FIXTURE_NAME}.json"
  err_path="$TMP_DIR/${FIXTURE_NAME}.stderr"
  vout_path="$TMP_DIR/${FIXTURE_NAME}.validate.log"

  set +e
  if [[ "$RUNTIME_ARENA_BYTES" == "0" ]]; then
    "$BIN" "$out_path" "$OWNER_KIND" >"$TMP_DIR/${FIXTURE_NAME}.stdout" 2>"$err_path"
  else
    "$BIN" "$out_path" "$OWNER_KIND" "$RUNTIME_ARENA_BYTES" >"$TMP_DIR/${FIXTURE_NAME}.stdout" 2>"$err_path"
  fi
  rc=$?
  set -e

  if [[ "$rc" -ne "$EXPECT_RC" ]]; then
    echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:rc_mismatch:expected=${EXPECT_RC}:actual=${rc}" >&2
    sed 's/^/  | /' "$err_path" >&2 || true
    exit 1
  fi

  if [[ "$EXPECT_RC" -eq 0 ]]; then
    if [[ -z "${EXPECT_OUTPUT:-}" ]]; then
      echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:missing_EXPECT_OUTPUT" >&2
      exit 1
    fi

    expected_path="$FIXTURE_DIR/$EXPECT_OUTPUT"
    if [[ ! -f "$expected_path" ]]; then
      echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:expected_output_missing:${EXPECT_OUTPUT}" >&2
      exit 1
    fi

    if ! cmp -s "$out_path" "$expected_path"; then
      echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:byte_drift" >&2
      exit 1
    fi

    if [[ "$EXPECT_VALIDATE_SCHEMA" -eq 1 ]]; then
      set +e
      bash "$VALIDATE" "$out_path" >"$vout_path" 2>&1
      vrc=$?
      set -e
      if [[ "$vrc" -ne 0 ]]; then
        echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:schema_reject" >&2
        sed 's/^/  | /' "$vout_path" >&2 || true
        exit 1
      fi
      if ! grep -Eq "MANIFEST_VALIDATE:(PASS|SUMMARY)" "$vout_path"; then
        echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:schema_marker_missing" >&2
        sed 's/^/  | /' "$vout_path" >&2 || true
        exit 1
      fi
    fi
  else
    ran_negative=1
    if [[ -n "${EXPECT_STDERR_SUBSTR:-}" ]]; then
      if ! grep -q "$EXPECT_STDERR_SUBSTR" "$err_path"; then
        echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:stderr_marker_missing" >&2
        sed 's/^/  | /' "$err_path" >&2 || true
        exit 1
      fi
    fi
    if [[ -s "$out_path" ]]; then
      echo "TEST:FAIL:manifestgen_golden:${FIXTURE_NAME}:unexpected_nonempty_output" >&2
      exit 1
    fi
  fi

  echo "TEST:PASS:manifestgen_golden:${FIXTURE_NAME}"
done

if [[ "$ran_any" -ne 1 ]]; then
  echo "TEST:FAIL:manifestgen_golden:no_fixtures_found" >&2
  exit 1
fi

if [[ "$ran_negative" -ne 1 ]]; then
  echo "TEST:FAIL:manifestgen_golden:no_negative_fixture" >&2
  exit 1
fi

echo "TEST:PASS:manifestgen_golden"
