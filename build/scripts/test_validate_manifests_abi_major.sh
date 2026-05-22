#!/usr/bin/env bash
# build/scripts/test_validate_manifests_abi_major.sh
#
# Issue #227: regression-test the --require-abi-major / --require-abi-major-from-header
# wiring added to tools/validate_manifests.py.
#
# Three checks:
#   1. PASS path: invoking the validator with the real header against the
#      real example manifests must succeed and emit MANIFEST_VALIDATE:PASS
#      for each manifest plus a SUMMARY:pass line.
#   2. FAIL path (explicit): forcing --require-abi-major=1 against the
#      current main-tree examples (which declare os_abi_version=0) must
#      exit non-zero AND emit a deterministic "does not match required"
#      marker for at least one manifest. This validates that a future
#      header bump to 1 without a lockstep manifest bump fails CI.
#   3. FAIL path (synthetic header): point --require-abi-major-from-header
#      at a generated header that defines OS_ABI_VERSION_MAJOR 1 and
#      confirm the same deterministic failure marker appears, validating
#      the parser path end-to-end.
#
# Exit codes:
#   0  - all three checks passed
#   1  - one of the checks regressed
#   78 - harness error (env/tool missing)
#
# Markers emitted on this script's own success path:
#   TEST:PASS:validate_manifests_abi_major

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WRAPPER="$ROOT_DIR/build/scripts/validate_manifests.sh"
HEADER="$ROOT_DIR/user/include/secureos_abi.h"

if [[ ! -x "$WRAPPER" && ! -r "$WRAPPER" ]]; then
  echo "TEST:FAIL:harness_missing_script:$WRAPPER" >&2
  exit 78
fi
if [[ ! -r "$HEADER" ]]; then
  echo "TEST:FAIL:harness_missing_header:$HEADER" >&2
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

# --- Check 1: pass with real header -------------------------------------
OUT="$TMP/check1.out"
if ! bash "$WRAPPER" --require-abi-major-from-header "$HEADER" >"$OUT" 2>&1; then
  echo "TEST:FAIL:validate_manifests_abi_major:pass_path_returned_nonzero" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi
if ! grep -q "MANIFEST_VALIDATE:SUMMARY:pass " "$OUT"; then
  echo "TEST:FAIL:validate_manifests_abi_major:pass_path_missing_summary" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

# --- Check 2: fail with --require-abi-major=1 ---------------------------
OUT="$TMP/check2.out"
set +e
bash "$WRAPPER" --require-abi-major=1 >"$OUT" 2>&1
RC=$?
set -e
if [[ "$RC" -eq 0 ]]; then
  echo "TEST:FAIL:validate_manifests_abi_major:explicit_fail_path_returned_zero" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi
if ! grep -q "does not match required OS_ABI_VERSION_MAJOR=1" "$OUT"; then
  echo "TEST:FAIL:validate_manifests_abi_major:explicit_fail_path_missing_marker" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

# --- Check 3: fail via synthetic header (parser path) -------------------
SYN_HEADER="$TMP/secureos_abi.h"
cat >"$SYN_HEADER" <<'EOF'
#ifndef SECUREOS_ABI_H
#define SECUREOS_ABI_H
#define OS_ABI_VERSION_MAJOR 1
#define OS_ABI_VERSION_MINOR 0
#define OS_ABI_VERSION ((OS_ABI_VERSION_MAJOR << 16) | OS_ABI_VERSION_MINOR)
#endif
EOF
OUT="$TMP/check3.out"
set +e
bash "$WRAPPER" --require-abi-major-from-header "$SYN_HEADER" >"$OUT" 2>&1
RC=$?
set -e
if [[ "$RC" -eq 0 ]]; then
  echo "TEST:FAIL:validate_manifests_abi_major:synthetic_fail_path_returned_zero" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi
if ! grep -q "does not match required OS_ABI_VERSION_MAJOR=1" "$OUT"; then
  echo "TEST:FAIL:validate_manifests_abi_major:synthetic_fail_path_missing_marker" >&2
  sed 's/^/  | /' "$OUT" >&2
  exit 1
fi

echo "TEST:PASS:validate_manifests_abi_major"
