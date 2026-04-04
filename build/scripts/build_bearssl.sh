#!/usr/bin/env bash
# build_bearssl.sh — Compile BearSSL objects for SecureOS freestanding x86-64.
#
# Purpose:
#   Compiles all BearSSL source files listed in Makefile.secureos into .o
#   files under artifacts/bearssl/ using the SecureOS freestanding user-app toolchain.
#
# Interactions:
#   - vendor/bearssl/Makefile.secureos lists the source files.
#   - vendor/bearssl/secureos_compat.c provides libc shims.
#   - build_kernel_entry.sh and build_user_app.sh link the resulting objects.
#
# Launched by:
#   Called from build.sh or directly before kernel/app builds.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BEARSSL_DIR="$ROOT_DIR/vendor/bearssl/BearSSL"
VENDOR_DIR="$ROOT_DIR/vendor/bearssl"
OUT_DIR="$ROOT_DIR/artifacts/bearssl"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

CC_FLAGS="--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector -mno-red-zone -nostdinc"

build_bearssl_inner() {
  local src_dir="$BEARSSL_DIR"
  local inc_dir="$BEARSSL_DIR/inc"

  if [ ! -d "$src_dir/src" ]; then
    echo "ERROR: BearSSL submodule not found. Run: git submodule update --init"
    exit 1
  fi

  mkdir -p "$OUT_DIR"

  # Compile compat shims (lives outside the submodule)
  echo "Compiling secureos_compat.c ..."
  clang $CC_FLAGS -I "$inc_dir" -I "$src_dir/src" \
    -c "$VENDOR_DIR/secureos_compat.c" -o "$OUT_DIR/secureos_compat.o"

  # Compile each BearSSL source file.
  # We enumerate from Makefile.secureos by extracting .c paths.
  local src_list
  src_list=$(grep '\.c' "$VENDOR_DIR/Makefile.secureos" | \
    sed 's/.*= *//' | sed 's/\\$//' | tr -s ' \n' '\n' | \
    grep '\.c$' | sort -u)

  local count=0
  for src_rel in $src_list; do
    local src_path="$src_dir/$src_rel"
    if [ ! -f "$src_path" ]; then
      echo "SKIP (not found): $src_rel"
      continue
    fi
    local obj_name
    obj_name=$(echo "$src_rel" | sed 's|/|_|g; s|\.c$|.o|')
    clang $CC_FLAGS -I "$inc_dir" -I "$src_dir/src" \
      -c "$src_path" -o "$OUT_DIR/$obj_name"
    count=$((count + 1))
  done

  echo "Compiled $count BearSSL objects into $OUT_DIR"
}

mkdir -p "$OUT_DIR"

if command -v docker >/dev/null 2>&1; then
  if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
    docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
  fi
  docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" \
    bash -lc 'set -euo pipefail; ./build/scripts/build_bearssl.sh'
else
  build_bearssl_inner
fi

echo "PASS: BearSSL build"