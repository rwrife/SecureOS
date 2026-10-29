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
#   check_dev_assets.sh [ROOT_DIR] [--archives] [--compiler]
#   --archives: require runtime archives after build.
#   --compiler: require freshly built cc.bin after build.
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
#   libsofpack.a, libmanifestgen.a, libtcc1.a) were produced before disk staging.
#   Passing --compiler additionally enforces the freshly built cc.bin (DEMO-04
#   #768: the developer image must ship the compiler, not just its archives).
set -euo pipefail

ROOT_DIR="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
if [ "$#" -gt 3 ] || [[ "${2:-}" != "" && "${2:-}" != "--archives" ]] || \
   [[ "${3:-}" != "" && "${3:-}" != "--compiler" ]]; then
	echo "Usage: check_dev_assets.sh [ROOT_DIR] [--archives] [--compiler]" >&2
	exit 2
fi
CHECK_ARCHIVES="${2:-}"
CHECK_COMPILER="${3:-}"

required_dev_assets=(
	dev/hello.c
	dev/pixels.c
	dev/building.txt
	dev/lib/README.md
	dev/tcc/README.md
	vendor/tinycc/tinycc/include/tccdefs.h
	user/libs/sofpack/include/sofpack/sofpack.h
	user/libs/manifestgen/include/manifestgen/manifest_default.h
	user/libs/clib/include/clib/stddef.h
	user/libs/clib/include/clib/stdarg.h
	user/libs/clib/include/clib/stdint.h
	user/libs/clib/include/clib/stdio.h
	user/libs/clib/include/clib/string.h
	user/libs/clib/include/clib/stdlib.h
	user/libs/clib/include/clib/errno.h
	user/libs/clib/include/clib/ctype.h
	user/libs/clib/include/clib/limits.h
	user/libs/clib/include/clib/stdbool.h
	user/include/secureos_api.h
	user/include/secureos_abi.h
)

if [ "$CHECK_ARCHIVES" = "--archives" ]; then
	required_dev_assets+=(
		artifacts/user/libs/libclib.a
		artifacts/user/libs/libsofpack.a
		artifacts/user/libs/libmanifestgen.a
		artifacts/user/libs/libtcc1.a
	)
fi

if [ "$CHECK_COMPILER" = "--compiler" ]; then
	required_dev_assets+=(
		artifacts/user/cc.bin
	)
fi

missing=0
for asset in "${required_dev_assets[@]}"; do
	if [ ! -f "$ROOT_DIR/$asset" ]; then
		echo "BUILD_DISK_IMAGE:FAIL:missing_required_dev_asset:$asset" >&2
		missing=1
	elif [[ "$asset" == artifacts/user/* ]] && [ ! -s "$ROOT_DIR/$asset" ]; then
		# ponytail: Check empty generated assets here; archive/SOF format is checked by build and image gates.
		echo "BUILD_DISK_IMAGE:FAIL:empty_required_dev_asset:$asset" >&2
		missing=1
	fi
done

if [ "$missing" -ne 0 ]; then
	exit 1
fi

echo "APPS_DEV_ASSETS:PASS:${#required_dev_assets[@]}"
