#!/usr/bin/env bash
# build/scripts/test_release_compliance_bundle_skip_pinned.sh
#
# Issue #553: drift gate for the release_compliance_bundle SKIP marker.
#
# Default mode (OPEN #408 phase):
#   - pins `TEST:SKIP:release_compliance_bundle:awaiting_408` in
#     build/scripts/test_release_compliance_bundle.sh and
#     docs/legal/lgpl-compliance.md.
#   - asserts docs section §5 cross-links issue #553 and records flip protocol.
#
# Strict mode (flip with #408 Phase 3):
#   - enabled via `--strict-no-skip` or RELEASE_COMPLIANCE_BUNDLE_STRICT_NO_SKIP=1
#   - asserts the SKIP marker is absent from script and docs.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT_PATH="$ROOT_DIR/build/scripts/test_release_compliance_bundle.sh"
DOC_PATH="$ROOT_DIR/docs/legal/lgpl-compliance.md"
EXPECTED_SKIP="TEST:SKIP:release_compliance_bundle:awaiting_408"
EXPECTED_ISSUE="#553"

STRICT=0
if [[ "${1:-}" == "--strict-no-skip" ]]; then
  STRICT=1
fi
if [[ "${RELEASE_COMPLIANCE_BUNDLE_STRICT_NO_SKIP:-0}" == "1" ]]; then
  STRICT=1
fi

emit_fail() {
  printf 'TEST:FAIL:release_compliance_bundle_skip_pinned:%s\n' "$1" >&2
}

if [[ ! -f "$SCRIPT_PATH" ]]; then
  emit_fail "missing_script"
  exit 1
fi
if [[ ! -f "$DOC_PATH" ]]; then
  emit_fail "missing_doc"
  exit 1
fi

SCRIPT_MARKERS_RAW="$(grep -Eo 'TEST:SKIP:release_compliance_bundle:[a-zA-Z0-9_]+' "$SCRIPT_PATH" || true)"
DOC_MARKERS_RAW="$(grep -Eo 'TEST:SKIP:release_compliance_bundle:[a-zA-Z0-9_]+' "$DOC_PATH" || true)"

SCRIPT_UNIQUE_COUNT="$(printf '%s\n' "$SCRIPT_MARKERS_RAW" | sed '/^$/d' | sort -u | wc -l | tr -d '[:space:]')"
DOC_UNIQUE_COUNT="$(printf '%s\n' "$DOC_MARKERS_RAW" | sed '/^$/d' | sort -u | wc -l | tr -d '[:space:]')"

if [[ "$STRICT" == "1" ]]; then
  if grep -Fq "$EXPECTED_SKIP" "$SCRIPT_PATH"; then
    emit_fail "strict_mode_skip_still_in_script"
    exit 1
  fi
  if grep -Fq "$EXPECTED_SKIP" "$DOC_PATH"; then
    emit_fail "strict_mode_skip_still_in_doc"
    exit 1
  fi
  if grep -Eq 'awaiting_408' "$DOC_PATH"; then
    emit_fail "strict_mode_awaiting_408_still_in_doc"
    exit 1
  fi

  printf 'TEST:PASS:release_compliance_bundle_skip_pinned:strict_script_skip_absent\n'
  printf 'TEST:PASS:release_compliance_bundle_skip_pinned:strict_doc_skip_absent\n'
  printf 'TEST:PASS:release_compliance_bundle_skip_pinned\n'
  exit 0
fi

if [[ "$SCRIPT_UNIQUE_COUNT" != "1" ]]; then
  emit_fail "script_skip_marker_count_drift"
  exit 1
fi
if [[ "$DOC_UNIQUE_COUNT" != "1" ]]; then
  emit_fail "doc_skip_marker_count_drift"
  exit 1
fi

SCRIPT_MARKER="$(printf '%s\n' "$SCRIPT_MARKERS_RAW" | sed '/^$/d' | sort -u | head -n1)"
DOC_MARKER="$(printf '%s\n' "$DOC_MARKERS_RAW" | sed '/^$/d' | sort -u | head -n1)"

if [[ "$SCRIPT_MARKER" != "$EXPECTED_SKIP" ]]; then
  emit_fail "script_skip_marker_drift"
  exit 1
fi
if [[ "$DOC_MARKER" != "$EXPECTED_SKIP" ]]; then
  emit_fail "doc_skip_marker_drift"
  exit 1
fi
if [[ "$SCRIPT_MARKER" != "$DOC_MARKER" ]]; then
  emit_fail "script_doc_marker_mismatch"
  exit 1
fi

if ! grep -Eq "${EXPECTED_ISSUE}|issue #553|Issue #553" "$DOC_PATH"; then
  emit_fail "doc_missing_issue_553_crosslink"
  exit 1
fi
if ! grep -Eq 'strict-no-skip|strict mode|strict-mode|strict' "$DOC_PATH"; then
  emit_fail "doc_missing_flip_protocol_strict_reference"
  exit 1
fi

printf 'TEST:PASS:release_compliance_bundle_skip_pinned:script_marker_pinned\n'
printf 'TEST:PASS:release_compliance_bundle_skip_pinned:doc_marker_pinned\n'
printf 'TEST:PASS:release_compliance_bundle_skip_pinned:doc_crosslinks_issue_553\n'
printf 'TEST:PASS:release_compliance_bundle_skip_pinned:doc_flip_protocol_present\n'
printf 'TEST:PASS:release_compliance_bundle_skip_pinned\n'
