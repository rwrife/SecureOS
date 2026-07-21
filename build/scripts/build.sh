#!/usr/bin/env bash
# build.sh - Internal build orchestrator (runs inside Docker container)
#
# This script runs INSIDE the Docker toolchain container. It orchestrates
# building all SecureOS components with automatic change detection to skip
# unchanged layers. Called by: scripts/build.sh (host-side) via docker run.
#
# Usage: build.sh [kernel|disk|all|app <name>|force]
#   kernel  - Compile kernel and create ISO
#   disk    - Build disk image with OS binaries
#   all     - Smart build: detect changes, only rebuild stale layers (default)
#   app     - Rebuild a single app and repack disk
#   force   - Rebuild everything regardless of changes
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TARGET="${1:-all}"

build_keys() {
  "$ROOT_DIR/build/scripts/generate_keys.sh"
}

build_bearssl() {
  "$ROOT_DIR/build/scripts/build_bearssl.sh"
}

build_kernel() {
  "$ROOT_DIR/build/scripts/build_kernel_entry.sh"
}

build_iso() {
  "$ROOT_DIR/build/scripts/build_kernel_image.sh"
}

build_libs() {
  if compgen -G "$ROOT_DIR/user/libs/*" >/dev/null 2>&1; then
    for lib_path in "$ROOT_DIR"/user/libs/*; do
      if [[ -d "$lib_path" ]]; then
        local lib_name
        lib_name="$(basename "$lib_path")"
        # Skip libs that aren't structured as a SOF user-lib (no main.c).
        # Header/src-only libs like `clib` (issue #404) are linked into
        # apps via the SDK path, not packed as a standalone .lib.
        # Exception: explicit archive-only freestanding libs that ship
        # sources under `src/` (e.g. `sofpack` #521 and `manifestgen` #579)
        # — these get built into `artifacts/user/libs/lib<name>.a` for
        # future on-target apps (the in-OS `cc` driver, #409) to link
        # against. Opt-in list so SDK-path libs like `clib` keep their
        # existing build flow.
        if [[ ! -f "$lib_path/main.c" ]]; then
          case "$lib_name" in
            sofpack|manifestgen)
              "$ROOT_DIR/build/scripts/build_user_lib.sh" "$lib_name"
              ;;
          esac
          continue
        fi
        "$ROOT_DIR/build/scripts/build_user_lib.sh" "$lib_name"
      fi
    done
  fi
}

build_apps() {
  # Build known user apps
  "$ROOT_DIR/build/scripts/build_user_app.sh" filedemo
  "$ROOT_DIR/build/scripts/build_user_app.sh" draw
  "$ROOT_DIR/build/scripts/build_user_app.sh" sosh
  "$ROOT_DIR/build/scripts/build_user_app.sh" win
}

build_disk() {
  "$ROOT_DIR/build/scripts/build_disk_image.sh"
}

# M6-SDK-002 (#388): assemble the userland `libos.a` published under
# `artifacts/sdk/`. Kept as a discrete step so the SDK surface can be
# (re)built without touching the kernel/disk layers.
build_sdk() {
  "$ROOT_DIR/build/scripts/build_sdk_libos.sh"
}

case "$TARGET" in
  kernel)
    build_keys
    build_kernel
    build_iso
    ;;
  disk)
    build_keys
    build_disk
    ;;
  sdk)
    # M6-SDK-002 (#388): build only the userland SDK archive.
    build_sdk
    ;;
  app)
    APP_NAME="${2:-}"
    if [ -z "$APP_NAME" ]; then
      echo "ERROR: app target requires a name. Usage: build.sh app <name>"
      exit 1
    fi
    build_keys
    "$ROOT_DIR/build/scripts/build_user_app.sh" "$APP_NAME"
    build_disk
    echo "[build] Rebuilt app '$APP_NAME' and repacked disk"
    ;;
  force)
    echo "[build] Force-rebuilding all components..."
    # Remove manifest to ensure full rebuild
    rm -f "$ROOT_DIR/artifacts/.build-manifest"
    build_keys
    build_bearssl
    build_kernel
    build_libs
    build_apps
    build_sdk
    build_iso
    build_disk
    "$ROOT_DIR/build/scripts/update_manifest.sh"
    echo "[build] All components force-built successfully"
    ;;
  all)
    echo "[build] Detecting changes..."
    STALE=$("$ROOT_DIR/build/scripts/detect_changes.sh")
    echo "[build] Stale layers: $STALE"

    if [ "$STALE" = "none" ]; then
      echo "[build] Everything up-to-date, nothing to rebuild"
      exit 0
    fi

    # Rebuild stale layers in dependency order
    if [[ "$STALE" == *keys* ]]; then
      echo "[build] Rebuilding: keys"
      build_keys
    fi

    if [[ "$STALE" == *bearssl* ]]; then
      echo "[build] Rebuilding: bearssl"
      build_bearssl
    fi

    if [[ "$STALE" == *kernel* ]]; then
      echo "[build] Rebuilding: kernel"
      build_kernel
    fi

    if [[ "$STALE" == *iso* ]]; then
      echo "[build] Rebuilding: iso"
      build_iso
    fi

    if [[ "$STALE" == *libs* ]]; then
      echo "[build] Rebuilding: libs"
      build_libs
    fi

    if [[ "$STALE" == *apps* ]]; then
      echo "[build] Rebuilding: apps"
      build_apps
    fi

    # M6-SDK-002 (#388): SDK archive piggy-backs on the libs/apps
    # layers since it shares the same freestanding toolchain flags.
    # detect_changes.sh has not been taught the `sdk` token yet (the
    # plan tracks that follow-up); rebuild opportunistically whenever
    # libs or apps were stale so external SDK consumers stay in sync.
    if [[ "$STALE" == *libs* || "$STALE" == *apps* ]]; then
      echo "[build] Rebuilding: sdk (piggybacks on libs/apps)"
      build_sdk
    fi

    if [[ "$STALE" == *disk* ]]; then
      echo "[build] Rebuilding: disk"
      build_disk
    fi

    # Update manifest after successful build
    "$ROOT_DIR/build/scripts/update_manifest.sh"
    echo "[build] Incremental build complete (rebuilt: $STALE)"
    ;;
  -h|--help)
    cat <<EOF
Usage: build.sh [TARGET]

Targets:
  all          Smart build: detect changes, only rebuild stale layers (default)
  kernel       Compile kernel and create ISO
  disk         Build disk image with OS binaries
  app <name>   Rebuild a single app and repack disk image
  sdk          Build artifacts/sdk/libos.a (M6-SDK-002, issue #388)
  force        Rebuild everything regardless of changes

The 'all' target uses a build manifest to track source file hashes and
automatically skips layers whose sources haven't changed since the last
successful build. On a clean checkout (no manifest), all layers are built.
EOF
    exit 0
    ;;
  *)
    echo "Unknown target: $TARGET"
    echo "Usage: build.sh [all|kernel|disk|sdk|app <name>|force|--help]"
    exit 1
    ;;
esac