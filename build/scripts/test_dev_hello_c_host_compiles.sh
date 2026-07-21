#!/usr/bin/env bash
# build/scripts/test_dev_hello_c_host_compiles.sh
#
# Host compile canary for the canonical in-OS toolchain validation sample
# (issue #574). Compiles dev/hello.c with freestanding-oriented flags so
# source drift is caught before M7 in-OS toolchain/qemu markers consume it.
#
# Contract:
#   - If no host C compiler is available, emit a deterministic SKIP marker
#     and return success (non-blocking on minimal environments).
#   - Otherwise, compile dev/hello.c with -Wall -Wextra -Werror,
#     -ffreestanding, -nostdlib, and user/include on the include path.
#
# Markers:
#   TEST:SKIP:dev_hello_c_host_compiles:no_host_compiler
#   TEST:PASS:dev_hello_c_host_compiles:compile_ok
#   TEST:PASS:dev_hello_c_host_compiles
#   TEST:FAIL:dev_hello_c_host_compiles:<reason>

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$ROOT_DIR/dev/hello.c"
OUT_DIR="$ROOT_DIR/artifacts/tests"
OBJ="$OUT_DIR/dev_hello_host_compile.o"
LOG="$OUT_DIR/dev_hello_host_compile.log"

emit() { printf '%s\n' "$1"; }

if [[ ! -f "$SRC" ]]; then
  emit "TEST:FAIL:dev_hello_c_host_compiles:missing_source:dev/hello.c"
  exit 1
fi

COMPILER=""
if [[ -n "${CC:-}" ]] && command -v "${CC}" >/dev/null 2>&1; then
  COMPILER="${CC}"
elif command -v cc >/dev/null 2>&1; then
  COMPILER="cc"
elif command -v clang >/dev/null 2>&1; then
  COMPILER="clang"
elif command -v gcc >/dev/null 2>&1; then
  COMPILER="gcc"
fi

if [[ -z "$COMPILER" ]]; then
  emit "TEST:SKIP:dev_hello_c_host_compiles:no_host_compiler"
  emit "TEST:PASS:dev_hello_c_host_compiles"
  exit 0
fi

mkdir -p "$OUT_DIR"

set +e
"$COMPILER" \
  -std=c11 -Wall -Wextra -Werror \
  -ffreestanding -nostdlib \
  -I "$ROOT_DIR/user/include" \
  -c "$SRC" \
  -o "$OBJ" \
  >"$LOG" 2>&1
RC=$?
set -e

if [[ "$RC" -ne 0 ]]; then
  emit "TEST:FAIL:dev_hello_c_host_compiles:compile_failed:compiler=$COMPILER:rc=$RC"
  exit 1
fi

emit "TEST:PASS:dev_hello_c_host_compiles:compile_ok"
emit "TEST:PASS:dev_hello_c_host_compiles"
