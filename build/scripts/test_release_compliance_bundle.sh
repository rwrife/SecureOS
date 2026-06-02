#!/usr/bin/env bash
# build/scripts/test_release_compliance_bundle.sh
#
# Issue #523: LGPL-2.1 compliance bundle gate.
#
# SKIP-pinned today (reason: awaiting_408) — same SKIP discipline as the
# M7-TOOLCHAIN acceptance suite (tests/m7_toolchain/*.sh). Becomes a
# required-PASS gate once #408 Phase 3 statically links libtcc into the
# shipped image.
#
# Even while SKIP-pinned, the script exercises
# build_release_compliance_bundle.sh end-to-end into a scratch directory
# and asserts the bundle layout + byte-identity of the license-text files
# against the in-tree submodule sources. That way the SKIP marker is
# advisory ("not yet required for release") but the scaffold itself
# cannot silently drift — a regression that breaks the bundle layout
# flips this gate to FAIL even before #408 lands.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT_DIR/build/scripts/build_release_compliance_bundle.sh"
SANDBOX="$(mktemp -d)"
trap 'rm -rf "$SANDBOX"' EXIT

if [[ ! -x "$SCRIPT" ]] && [[ ! -r "$SCRIPT" ]]; then
  printf 'TEST:FAIL:release_compliance_bundle:missing_build_script\n' >&2
  exit 1
fi

OUT_DIR="$SANDBOX/compliance" bash "$SCRIPT" >"$SANDBOX/out.log" 2>&1 || {
  cat "$SANDBOX/out.log" >&2
  printf 'TEST:FAIL:release_compliance_bundle:build_script_nonzero\n' >&2
  exit 1
}

BUNDLE="$SANDBOX/compliance"

# Required filenames (mirrors docs/legal/lgpl-compliance.md §3).
required_files=(
  "$BUNDLE/README.md"
  "$BUNDLE/LICENSE.tinycc"
  "$BUNDLE/LICENSE.bearssl"
  "$BUNDLE/ATTRIBUTION.md"
  "$BUNDLE/SOURCE_URL.txt"
  "$BUNDLE/tinycc-src.tar.gz"
  "$BUNDLE/relink/README.md"
  "$BUNDLE/relink/libtcc.o"
  "$BUNDLE/relink/secureos-objs.tar.gz"
)
for f in "${required_files[@]}"; do
  if [[ ! -f "$f" ]]; then
    printf 'TEST:FAIL:release_compliance_bundle:missing_file:%s\n' "${f#$BUNDLE/}" >&2
    exit 1
  fi
done

# Byte-identity checks when the source submodules are present.
TINYCC_COPYING="$ROOT_DIR/vendor/tinycc/tinycc/COPYING"
if [[ -f "$TINYCC_COPYING" ]]; then
  if ! cmp -s "$TINYCC_COPYING" "$BUNDLE/LICENSE.tinycc"; then
    printf 'TEST:FAIL:release_compliance_bundle:license_tinycc_drift\n' >&2
    exit 1
  fi
fi
BEARSSL_LICENSE="$ROOT_DIR/vendor/bearssl/BearSSL/LICENSE.txt"
if [[ -f "$BEARSSL_LICENSE" ]]; then
  if ! cmp -s "$BEARSSL_LICENSE" "$BUNDLE/LICENSE.bearssl"; then
    printf 'TEST:FAIL:release_compliance_bundle:license_bearssl_drift\n' >&2
    exit 1
  fi
fi

# Source URL pointer must name the canonical repo and a non-empty commit.
if ! grep -q "github.com/rwrife/SecureOS" "$BUNDLE/SOURCE_URL.txt"; then
  printf 'TEST:FAIL:release_compliance_bundle:source_url_missing_repo\n' >&2
  exit 1
fi
if ! grep -Eq 'Release commit: +[0-9a-f]{7,}|Release commit: +unknown' "$BUNDLE/SOURCE_URL.txt"; then
  printf 'TEST:FAIL:release_compliance_bundle:source_url_missing_commit\n' >&2
  exit 1
fi

# Bundle's own README references the normative doc.
if ! grep -q "docs/legal/lgpl-compliance.md" "$BUNDLE/README.md"; then
  printf 'TEST:FAIL:release_compliance_bundle:readme_missing_doc_ref\n' >&2
  exit 1
fi

# SKIP discipline (same shape as tests/m7_toolchain/*.sh): until #408
# Phase 3 actually links libtcc into the shipped image, the gate is
# advisory. The scaffold checks above still ran; they will continue to
# fail-fast if the bundle layout regresses. Once #408 Phase 3 lands the
# SKIP line should be removed (and the PHASE3_LANDED marker emitted by
# the build script — TBD in #408's PR).
printf 'TEST:SKIP:release_compliance_bundle:awaiting_408\n'
printf 'TEST:PASS:release_compliance_bundle\n'
