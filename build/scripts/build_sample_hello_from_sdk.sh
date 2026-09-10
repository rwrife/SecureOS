#!/usr/bin/env bash
# @file build_sample_hello_from_sdk.sh
# @brief Build the SDK sample app skeleton for issue #584 (M6-SDK-004 starter).
#
# Purpose:
#   Produces a host-verifiable build artifact for `samples/hello-from-sdk/`
#   without requiring the not-yet-landed `os-cc` / `os-pack` / `os-run`
#   wrappers from issue #396.
#
# What this script asserts:
#   1. `samples/hello-from-sdk/main.c` compiles with SDK include paths.
#   2. The sample links against an SDK `libos.a` composition built from
#      `sdk/lib/crt0.c`, `sdk/lib/libos/version.c`, and
#      `user/runtime/secureos_api_stubs.c` (same slice-2 source set as #388).
#   3. The sample manifest validates against `manifests/schema/v0.json`.
#
# Launched by:
#   build/scripts/test_m6_sample_hello_from_sdk.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SAMPLE_DIR="$ROOT_DIR/samples/hello-from-sdk"
OUT_DIR="$ROOT_DIR/artifacts/tests/m6_sample_hello_from_sdk"
mkdir -p "$OUT_DIR"

CC="${CC:-cc}"
AR="${AR:-ar}"
LD="${LD:-ld}"
NM="${NM:-nm}"

SRC="$SAMPLE_DIR/main.c"
MANIFEST="$SAMPLE_DIR/manifest.json"
OBJ="$OUT_DIR/hello_from_sdk.o"
LINKED_O="$OUT_DIR/hello_from_sdk.linked.o"
NM_DUMP="$OUT_DIR/hello_from_sdk.nm.txt"
CRT0_O="$OUT_DIR/crt0.o"
VERSION_O="$OUT_DIR/version.o"
STUBS_O="$OUT_DIR/secureos_api_stubs.o"
LIBOS_A="$OUT_DIR/libos.host.a"

if [[ ! -r "$SRC" ]]; then
  echo "BUILD_SAMPLE_HELLO_FROM_SDK:FAIL:missing_source:$SRC" >&2
  exit 1
fi
if [[ ! -r "$MANIFEST" ]]; then
  echo "BUILD_SAMPLE_HELLO_FROM_SDK:FAIL:missing_manifest:$MANIFEST" >&2
  exit 1
fi

for tool in "$CC" "$AR" "$LD" "$NM"; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "BUILD_SAMPLE_HELLO_FROM_SDK:FAIL:missing_tool:$tool" >&2
    exit 1
  fi
done

HOST_ARCH="$(uname -m 2>/dev/null || echo unknown)"
case "$HOST_ARCH" in
  x86_64|amd64) ;;
  *)
    echo "BUILD_SAMPLE_HELLO_FROM_SDK:SKIP:host_arch_not_x86_64:$HOST_ARCH"
    exit 0
    ;;
esac

HOST_CFLAGS="-std=c11 -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch"
HOST_CFLAGS="$HOST_CFLAGS -I$ROOT_DIR/sdk/include -I$ROOT_DIR/user/include"

"$CC" $HOST_CFLAGS -c "$ROOT_DIR/sdk/lib/crt0.c" -o "$CRT0_O"
"$CC" $HOST_CFLAGS -c "$ROOT_DIR/sdk/lib/libos/version.c" -o "$VERSION_O"
"$CC" $HOST_CFLAGS -c "$ROOT_DIR/user/runtime/secureos_api_stubs.c" -o "$STUBS_O"
"$CC" $HOST_CFLAGS -c "$SRC" -o "$OBJ"

rm -f "$LIBOS_A"
"$AR" Drcs "$LIBOS_A" "$CRT0_O" "$VERSION_O" "$STUBS_O"

"$LD" -r --whole-archive "$OBJ" "$LIBOS_A" --no-whole-archive -o "$LINKED_O"
"$NM" "$LINKED_O" > "$NM_DUMP"

bash "$ROOT_DIR/build/scripts/validate_manifests.sh" "$MANIFEST" >/dev/null

echo "BUILD_SAMPLE_HELLO_FROM_SDK:PASS:compiled:$OBJ"
echo "BUILD_SAMPLE_HELLO_FROM_SDK:PASS:linked:$LINKED_O"
echo "BUILD_SAMPLE_HELLO_FROM_SDK:PASS:nm_dump:$NM_DUMP"
echo "BUILD_SAMPLE_HELLO_FROM_SDK:PASS:manifest_valid:$MANIFEST"
echo "BUILD_SAMPLE_HELLO_FROM_SDK:PASS"
