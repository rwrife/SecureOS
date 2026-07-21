#!/usr/bin/env bash
# build/scripts/test_manifestgen_audit_marker_format.sh
#
# Issue #594: host format-contract gate for libmanifestgen synthesis audit
# markers (`manifest.synth.ok` / `manifest.synth.fail`).
#
# Asserts:
#   - success marker exact shape
#     manifest.synth.ok:<sid>:<sof_sha_prefix>:<owner_kind>:<arena_bytes>
#   - fail marker exact shape
#     manifest.synth.fail:<sid>:<reason_enum>
#   - representative fail reasons are stable (`bad_owner_kind`,
#     `bad_arena_bytes`, `bad_required_fields`, `bad_args`).
#
# Exit codes:
#   0  - pass
#   1  - regression
#   78 - harness/tooling error

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/artifacts/tests"
SRC_TEST="$ROOT_DIR/tests/manifestgen_audit_marker_format_test.c"
SRC_LIB="$ROOT_DIR/user/libs/manifestgen/src/manifest_default.c"
BIN="$OUT_DIR/manifestgen_audit_marker_format_test"
LOG="$OUT_DIR/manifestgen_audit_marker_format_test.log"

mkdir -p "$OUT_DIR"

CC="${CC:-gcc}"
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "TEST:FAIL:harness_missing_cc:$CC" >&2
  exit 78
fi

if [[ ! -r "$SRC_TEST" || ! -r "$SRC_LIB" ]]; then
  echo "TEST:FAIL:harness_missing_source" >&2
  exit 78
fi

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
  "$SRC_TEST" \
  "$SRC_LIB" \
  -o "$BIN"

"$BIN" | tee "$LOG"

grep -q "TEST:PASS:manifestgen_audit_marker_format:ok_marker$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format:fail_bad_owner_kind$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format:fail_bad_arena_bytes$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format:fail_bad_required_fields$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format:fail_bad_args$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format:guardrails$" "$LOG"
grep -q "TEST:PASS:manifestgen_audit_marker_format$" "$LOG"
