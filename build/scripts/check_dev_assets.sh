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
#   check_dev_assets.sh [ROOT_DIR]     # defaults to the repository root
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
#   Compiler/runtime archives (libclib.a, sofpack.a, libtcc1.a) are NOT part of
#   this list. They are host build artifacts whose staging is still conditional
#   under #768, so requiring them here would break every clean image build.
set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"

required_dev_assets=(
	dev/hello.c
	dev/building.txt
	dev/lib/README.md
	dev/tcc/README.md
	user/libs/sofpack/include/sofpack/sofpack.h
	user/libs/manifestgen/include/manifestgen/manifest_default.h
)

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
