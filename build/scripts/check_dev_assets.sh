#!/usr/bin/env bash
# check_dev_assets.sh - Fail-closed presence gate for tracked /apps/dev assets
#
# Purpose:
#   Declares the authoritative list of repository-tracked developer assets that
#   build/scripts/build_disk_image.sh stages unconditionally into /apps/dev, and
#   verifies each one exists before the disk build does any real work. A missing
#   asset is a build failure (DEMO-04 #768: "missing required developer assets a
#   build failure rather than silently skipping them"), not a skipped mapping.
#
# Usage:
#   check_dev_assets.sh [ROOT_DIR] [--archives] # defaults to repository root
#   --archives: also require host-built archives after the disk build creates them.
#
# Called by:
#   - build/scripts/build_disk_image.sh (early, before keys/compilers/ISO work)
#   - tests/in_os_toolchain_dev_dir_test.py (negative case in a temp root)
#
# Output:
#   APPS_DEV_ASSETS:PASS:<count> on success.
#   BUILD_DISK_IMAGE:FAIL:missing_required_dev_asset:<path> per missing file on
#   stderr, exit 1.
#
# Scope note:
#   Tracked repository assets are checked early before build work begins.
#   Passing --archives enforces that compiler/runtime archives (libclib.a,
#   libsofpack.a, libtcc1.a) were produced by the build before disk staging.
set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
if [ "$#" -gt 2 ] || [[ "${2:-}" != "" && "${2:-}" != "--archives" ]]; then
	echo "Usage: check_dev_assets.sh [ROOT_DIR] [--archives]" >&2
	exit 2
fi
CHECK_ARCHIVES="${2:-}"

required_dev_assets=(
	dev/hello.c
	dev/building.txt
	dev/lib/README.md
	dev/tcc/README.md
	user/libs/sofpack/include/sofpack/sofpack.h
	user/libs/manifestgen/include/manifestgen/manifest_default.h
)

if [ "$CHECK_ARCHIVES" = "--archives" ]; then
	required_dev_assets+=(
		artifacts/user/libs/libclib.a
		artifacts/user/libs/libsofpack.a
		artifacts/user/libs/libtcc1.a
	)
fi

missing=0
for asset in "${required_dev_assets[@]}"; do
	if [ ! -f "$ROOT_DIR/$asset" ]; then
		echo "BUILD_DISK_IMAGE:FAIL:missing_required_dev_asset:$asset" >&2
		missing=1
	fi
done

if [ "$missing" -ne 0 ]; then
	exit 1
fi

echo "APPS_DEV_ASSETS:PASS:${#required_dev_assets[@]}"
