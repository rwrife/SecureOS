#!/usr/bin/env bash
# test_bearssl_compile.sh — Deterministic validator for the vendored
# BearSSL slice (issue #117).
#
# Purpose:
#   Asserts the BearSSL vendor slice is well-formed:
#     - Makefile.secureos enumerates the .c subset explicitly (no globbing).
#     - secureos_compat.c provides the freestanding libc shims BearSSL needs
#       and compiles cleanly with -Wall -Werror as a host-side syntax check
#       (we cannot run the freestanding clang cross-compile in every CI
#       environment, but the shim file is portable C and a host compile is a
#       strong proxy for "the file is well-formed").
#     - VERSION pin file is present and references the submodule commit.
#     - When a previous full build has populated artifacts/bearssl/, also
#       asserts object count > 0 and that the total compiled size is within
#       the implementation_plan.md ~80-100KB budget (with slack).
#
# Emits TEST:PASS:<name> / TEST:FAIL:<name>:<reason> markers so the
# validator JSON report (#110) can key on individual sub-checks.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor/bearssl"
OUT_DIR="$ROOT_DIR/artifacts/bearssl"

echo "TEST:START:bearssl_compile"

fail() {
  echo "TEST:FAIL:bearssl_compile:$1"
  exit 1
}

# 1. Makefile.secureos exists and enumerates a non-empty, deterministic .c
#    list (no glob expansion).
if [ ! -f "$VENDOR_DIR/Makefile.secureos" ]; then
  fail "missing_makefile_secureos"
fi

if grep -E '\*\.c|wildcard' "$VENDOR_DIR/Makefile.secureos" >/dev/null 2>&1; then
  fail "makefile_uses_globbing"
fi

SRC_LIST="$(grep '\.c' "$VENDOR_DIR/Makefile.secureos" | \
  sed 's/.*= *//' | sed 's/\\$//' | tr -s ' \n' '\n' | \
  grep '\.c$' | sort -u)"
SRC_COUNT=$(printf '%s\n' "$SRC_LIST" | grep -c .)
if [ "$SRC_COUNT" -lt 60 ]; then
  fail "makefile_src_list_too_small:$SRC_COUNT"
fi
echo "TEST:PASS:bearssl_compile_makefile_enumerated:$SRC_COUNT"

# 2. VERSION pin file references the submodule commit.
if [ ! -f "$VENDOR_DIR/VERSION" ]; then
  fail "missing_version_pin"
fi
if ! grep -E 'Commit:[[:space:]]+[0-9a-f]{40}' "$VENDOR_DIR/VERSION" >/dev/null; then
  fail "version_pin_missing_commit"
fi
echo "TEST:PASS:bearssl_compile_version_pinned"

# 3. secureos_compat.c exists and provides the five required shims.
if [ ! -f "$VENDOR_DIR/secureos_compat.c" ]; then
  fail "missing_compat_shim_source"
fi
for sym in memcpy memmove memset memcmp strlen; do
  if ! grep -E "[[:space:]]\\*?$sym\\(" "$VENDOR_DIR/secureos_compat.c" >/dev/null; then
    fail "compat_shim_missing_symbol:$sym"
  fi
done
echo "TEST:PASS:bearssl_compile_compat_shims_present"

# 4. Host-side syntax check on secureos_compat.c.
HOST_OUT="$ROOT_DIR/artifacts/tests"
mkdir -p "$HOST_OUT"
if cc -std=c11 -Wall -Werror -ffreestanding -nostdlib \
    -c "$VENDOR_DIR/secureos_compat.c" \
    -o "$HOST_OUT/secureos_compat_host.o" >/dev/null 2>&1; then
  echo "TEST:PASS:bearssl_compile_compat_shim_host_compiles"
else
  fail "compat_shim_host_compile_failed"
fi

# 5. Optional: when a previous full freestanding build has populated
#    artifacts/bearssl/, sanity-check object count and code-size budget.
if [ -d "$OUT_DIR" ] && [ -n "$(ls -A "$OUT_DIR" 2>/dev/null || true)" ]; then
  OBJ_COUNT=$(find "$OUT_DIR" -name '*.o' | wc -l | tr -d ' ')
  if [ "$OBJ_COUNT" -lt 60 ]; then
    fail "freestanding_object_count_too_small:$OBJ_COUNT"
  fi
  TOTAL_KB=$(du -sk "$OUT_DIR" | awk '{print $1}')
  # Budget per implementation_plan.md is ~80-100KB compiled. Allow slack
  # (debug symbols, alignment, future addition of inc/* helpers) up to 1MB.
  if [ "$TOTAL_KB" -gt 1024 ]; then
    fail "freestanding_object_size_over_budget:${TOTAL_KB}KB"
  fi
  echo "TEST:PASS:bearssl_compile_freestanding_objects:$OBJ_COUNT:${TOTAL_KB}KB"
else
  echo "TEST:SKIP:bearssl_compile_freestanding_objects:no_artifacts_yet"
fi

echo "TEST:PASS:bearssl_compile"
