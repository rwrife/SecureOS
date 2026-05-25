#!/usr/bin/env bash
# detect_changes.sh - Determine which build layers are stale
#
# This script runs INSIDE the Docker toolchain container. It computes a
# combined hash of all source files for each build layer and compares
# against the stored manifest from the last successful build.
#
# Output: space-separated list of stale layer names, or "none" if all
# layers are up-to-date.  Layers: keys bearssl kernel iso libs apps
# os_commands disk
#
# Called by: build/scripts/build.sh (smart mode)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$ROOT_DIR/artifacts/.build-manifest"

# Compute a combined hash of files matching given glob patterns.
# Usage: compute_layer_hash pattern1 [pattern2 ...]
compute_layer_hash() {
  local hash_input=""
  local pattern
  for pattern in "$@"; do
    # Use find with the pattern base dir; fall back to empty if no matches
    while IFS= read -r -d '' file; do
      hash_input+="$(md5sum "$file" 2>/dev/null | cut -d' ' -f1)"
    done < <(eval "find $pattern -type f -print0 2>/dev/null" || true)
  done

  if [ -z "$hash_input" ]; then
    echo "empty"
  else
    echo "$hash_input" | md5sum | cut -d' ' -f1
  fi
}

# Compute hash by listing files via find with proper glob expansion
hash_files() {
  local search_dirs=("$@")
  local hash_input=""
  local file

  for dir_or_glob in "${search_dirs[@]}"; do
    if [ -d "$dir_or_glob" ]; then
      while IFS= read -r -d '' file; do
        hash_input+="$(md5sum "$file" 2>/dev/null | cut -d' ' -f1)"
      done < <(find "$dir_or_glob" -type f -print0 2>/dev/null | sort -z)
    elif compgen -G "$dir_or_glob" >/dev/null 2>&1; then
      for file in $dir_or_glob; do
        [ -f "$file" ] && hash_input+="$(md5sum "$file" 2>/dev/null | cut -d' ' -f1)"
      done
    fi
  done

  if [ -z "$hash_input" ]; then
    echo "empty"
  else
    echo "$hash_input" | md5sum | cut -d' ' -f1
  fi
}

# Read stored hash for a layer from the manifest
get_manifest_hash() {
  local layer="$1"
  if [ ! -f "$MANIFEST" ]; then
    echo ""
    return
  fi
  grep "^${layer}=" "$MANIFEST" 2>/dev/null | cut -d'=' -f2 || echo ""
}

# --- Compute current hashes for each layer ---
cd "$ROOT_DIR"

HASH_KEYS=$(hash_files "build/scripts/generate_keys.sh" "tools/keygen/")
HASH_BEARSSL=$(hash_files "vendor/bearssl/")
HASH_KERNEL=$(hash_files "kernel/")
HASH_GRUB=$(hash_files "build/grub/")
HASH_LIBS=$(hash_files "user/libs/" "user/include/")
HASH_APPS=$(hash_files "user/apps/" "user/include/" "user/runtime/")
HASH_OS_CMDS=$(hash_files "user/os_commands/")
HASH_BUILD_SCRIPTS=$(hash_files "build/scripts/build_user_app.sh" "build/scripts/build_user_lib.sh" "build/scripts/build_os_command.sh" "build/scripts/build_kernel_entry.sh" "build/scripts/build_kernel_image.sh")

# --- Compare against manifest ---
STALE=""

STORED_KEYS=$(get_manifest_hash "keys")
STORED_BEARSSL=$(get_manifest_hash "bearssl")
STORED_KERNEL=$(get_manifest_hash "kernel")
STORED_GRUB=$(get_manifest_hash "grub")
STORED_LIBS=$(get_manifest_hash "libs")
STORED_APPS=$(get_manifest_hash "apps")
STORED_OS_CMDS=$(get_manifest_hash "os_commands")
STORED_BUILD_SCRIPTS=$(get_manifest_hash "build_scripts")

# Check keys
if [ "$HASH_KEYS" != "$STORED_KEYS" ] || [ ! -f "artifacts/keys/root.pub" ]; then
  STALE="$STALE keys"
fi

# Check bearssl
if [ "$HASH_BEARSSL" != "$STORED_BEARSSL" ] || [ ! -d "artifacts/bearssl" ]; then
  STALE="$STALE bearssl"
fi

# Check kernel (also stale if build scripts changed)
if [ "$HASH_KERNEL" != "$STORED_KERNEL" ] || [ "$HASH_BUILD_SCRIPTS" != "$STORED_BUILD_SCRIPTS" ] || [ ! -f "artifacts/kernel/kernel.elf" ]; then
  STALE="$STALE kernel"
fi

# Check ISO (stale if kernel is stale or grub config changed)
if [[ "$STALE" == *kernel* ]] || [ "$HASH_GRUB" != "$STORED_GRUB" ] || [ ! -f "artifacts/kernel/secureos.iso" ]; then
  STALE="$STALE iso"
fi

# Check libs (also stale if bearssl changed, since netlib depends on it)
if [ "$HASH_LIBS" != "$STORED_LIBS" ] || [[ "$STALE" == *bearssl* ]]; then
  STALE="$STALE libs"
fi

# Check apps (stale if libs changed or app sources changed)
if [ "$HASH_APPS" != "$STORED_APPS" ] || [[ "$STALE" == *libs* ]]; then
  STALE="$STALE apps"
fi

# Check os_commands
if [ "$HASH_OS_CMDS" != "$STORED_OS_CMDS" ]; then
  STALE="$STALE os_commands"
fi

# Disk is stale if any userspace layer changed
if [[ "$STALE" == *libs* ]] || [[ "$STALE" == *apps* ]] || [[ "$STALE" == *os_commands* ]] || [ ! -f "artifacts/disk/secureos-disk.img" ]; then
  STALE="$STALE disk"
fi

# Trim leading space and output
STALE="$(echo "$STALE" | xargs)"
if [ -z "$STALE" ]; then
  echo "none"
else
  echo "$STALE"
fi
