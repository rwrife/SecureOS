#!/usr/bin/env bash
# build_tinycc_libtcc1.sh — Compile the TinyCC runtime-helper archive
# `libtcc1.a` for the SecureOS freestanding x86-64 target (issue #766,
# #408 Phase 3 first-compile slice).
#
# Purpose:
#   Builds artifacts/user/libs/libtcc1.a — the runtime-helper archive
#   TinyCC links INTO every program it compiles on-target. The TU set is
#   NOT hardcoded here: it is read from vendor/tinycc/libtcc1-srcs.json
#   (the #548 drift-pinned partition of vendor/tinycc/tinycc/lib,
#   enforced by the `tinycc_libtcc1_srcs` gate), so a TinyCC submodule
#   bump that renames/adds a runtime TU fails the drift gate before this
#   script silently builds the wrong surface.
#
# Interactions:
#   - vendor/tinycc/libtcc1-srcs.json   (authoritative TU list)
#   - vendor/tinycc/config-secureos.h   (build configuration, #519)
#   - vendor/tinycc/include/            (config.h redirect + system
#                                        header shims for the freestanding
#                                        include graph, issue #766)
#   - user/libs/clib/include/clib/      (freestanding libc headers)
#   - build/scripts/build_disk_image.sh stages the produced archive to
#     /apps/dev/tcc/libtcc1.a when present (issue #550 path, unchanged).
#
# Launched by:
#   build/scripts/build.sh (tinycc layer) and the
#   `tinycc_freestanding_compile` host gate.
#
# Determinism: object mtimes are normalized to the epoch before archiving
# and the TU list order is fixed by the manifest, so repeated builds in
# the same toolchain produce byte-identical archives (BUILD_ROADMAP §4.4).

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENDOR_DIR="$ROOT_DIR/vendor/tinycc"
SUBMODULE_DIR="$VENDOR_DIR/tinycc"
MANIFEST="$VENDOR_DIR/libtcc1-srcs.json"
OUT_DIR="$ROOT_DIR/artifacts/user/libs"
OBJ_DIR="$ROOT_DIR/artifacts/tinycc/libtcc1-obj"

# Freestanding userland toolchain flags — mirror build_bearssl.sh /
# build_user_app.sh so the archive is link-compatible with user apps.
CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdlibinc"
CC_WARN_FLAGS="-Wall -Werror -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-typedef-redefinition"
INCLUDES="-I $VENDOR_DIR/include -I $ROOT_DIR/user/include -I $ROOT_DIR/user/libs/clib/include/clib -I $SUBMODULE_DIR"

if ! command -v clang >/dev/null 2>&1; then
  echo "ERROR: clang not found — run inside the pinned toolchain image" >&2
  echo "  (scripts/build.sh) or a container with clang installed." >&2
  exit 1
fi

if [ ! -d "$SUBMODULE_DIR" ] || [ -z "$(ls -A "$SUBMODULE_DIR" 2>/dev/null)" ]; then
  echo "ERROR: TinyCC submodule not found. Run: git submodule update --init" >&2
  exit 1
fi

if [ ! -f "$MANIFEST" ]; then
  echo "ERROR: missing TU manifest $MANIFEST" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 required to read libtcc1-srcs.json" >&2
  exit 1
fi

# Extract the required TU list (manifest order is the archive order).
TU_LIST="$(python3 -c '
import json, sys
m = json.load(open(sys.argv[1]))
for e in m["required"]:
    print(e["path"])
' "$MANIFEST")"

if [ -z "$TU_LIST" ]; then
  echo "ERROR: libtcc1-srcs.json required list is empty" >&2
  exit 1
fi

# TCC_VERSION from the pinned VERSION file (short commit form, per
# config-secureos.h's note — avoids hard-coding a version string).
TCC_VERSION="3b1fe97a59"
if [ -f "$VENDOR_DIR/VERSION" ]; then
  TCC_VERSION="$(grep -E '^Commit:[[:space:]]+[0-9a-f]{40}[[:space:]]*$' "$VENDOR_DIR/VERSION" \
    | awk '{print $2}' | cut -c1-10 || echo 3b1fe97a59)"
fi

mkdir -p "$OUT_DIR" "$OBJ_DIR"

count=0
objs=""
while IFS= read -r tu; do
  [ -z "$tu" ] && continue
  src="$SUBMODULE_DIR/lib/$tu"
  if [ ! -f "$src" ]; then
    echo "ERROR: pinned TU missing from submodule: lib/$tu" >&2
    exit 1
  fi
  obj="$OBJ_DIR/${tu//\//_}.o"   # libtcc1.c -> libtcc1.c.o, atomic.S -> atomic.S.o
  # .S files need the preprocessor + same target; clang drives both.
  clang $CC_FLAGS $CC_WARN_FLAGS -include "$VENDOR_DIR/config-secureos.h" \
    -DTCC_VERSION="\"$TCC_VERSION\"" $INCLUDES \
    -c "$src" -o "$obj"
  objs="$objs $obj"
  count=$((count + 1))
done <<EOF
$TU_LIST
EOF

# Deterministic archive: normalize member metadata before archiving.
# binutils ar is deterministic-by-default for uid/gid/mode; mtime is the
# remaining variable input, pinned to the epoch.
find "$OBJ_DIR" -name '*.o' -exec touch -h -d "@0" {} +

ARCHIVE="$OUT_DIR/libtcc1.a"
rm -f "$ARCHIVE"
# shellcheck disable=SC2086
ar rcs "$ARCHIVE" $objs

echo "LIBTCC1_TU_COUNT=$count"
echo "LIBTCC1_ARCHIVE=$ARCHIVE"
if command -v du >/dev/null 2>&1; then
  echo "LIBTCC1_ARCHIVE_KB=$(du -sk "$ARCHIVE" 2>/dev/null | awk '{print $1}')"
fi
echo "libtcc1.a built: $count TUs from $MANIFEST"
