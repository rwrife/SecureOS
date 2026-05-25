#!/usr/bin/env bash
# build_disk_image.sh - Create FAT disk image with OS binaries and apps
#
# This script runs INSIDE the Docker toolchain container. It builds all
# user-space components and packs them into a FAT disk image.
# Called by: build/scripts/build.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DISK_DIR="$ROOT_DIR/artifacts/disk"
DISK_PATH="$DISK_DIR/secureos-disk.img"
DISK_BLOCKS="${SECUREOS_DISK_BLOCKS:-8192}"

# Issue #226: gate the disk-image build on manifest validity, not just
# schema validity (follow-up to #219). Validate every manifest the
# build pipeline would otherwise stage onto secureos-disk.img — the
# curated examples under manifests/examples/ AND any per-app manifest
# dropped under user/apps/**/*.manifest.json. Run this on the host
# (outside the toolchain container) because the container image does
# not ship python3-jsonschema; the validator wrapper itself will
# self-diagnose with MANIFEST_VALIDATE:ERROR if the dependency is
# missing.
#
# If jsonschema is unavailable on this host we fall back to a warning
# rather than failing the build, so contributors without the lib can
# still rebuild the disk locally; CI installs jsonschema in the
# build-and-validate job (see .github/workflows/pr-build.yml) and
# always exercises the strict path.
run_manifest_gate() {
	local py="${PYTHON:-python3}"
	if ! command -v "$py" >/dev/null 2>&1; then
		echo "build_disk_image: python3 not found, skipping manifest gate (issue #226)" >&2
		return 0
	fi
	if ! "$py" -c "import jsonschema" >/dev/null 2>&1; then
		echo "build_disk_image: python3-jsonschema not installed, skipping manifest gate (issue #226)" >&2
		return 0
	fi
	"$ROOT_DIR/build/scripts/validate_manifests.sh" \
		--require-abi-major-from-header user/include/secureos_abi.h \
		'manifests/examples/*.json' \
		'user/apps/**/*.manifest.json'
}

run_manifest_gate

build_disk_image_inner() {
	local script_path
	local cmd_name
	local lib_path
	local lib_name
	local os_app
	local -a app_mappings=()

	# Ensure signing keys exist (generate if missing)
	if [ ! -f "$ROOT_DIR/artifacts/keys/intermediate.seed" ]; then
		"$ROOT_DIR/build/scripts/generate_keys.sh"
	fi

	mkdir -p "$DISK_DIR" "$ROOT_DIR/artifacts/os" "$ROOT_DIR/artifacts/lib"

	if compgen -G "user/os_commands/*.cmd" >/dev/null 2>&1; then
		for script_path in user/os_commands/*.cmd; do
			cmd_name="$(basename "$script_path" .cmd)"
			./build/scripts/build_os_command.sh "$cmd_name"
		done
	fi

	if compgen -G "user/libs/*" >/dev/null 2>&1; then
		for lib_path in user/libs/*; do
			if [[ -d "$lib_path" ]]; then
				lib_name="$(basename "$lib_path")"
				./build/scripts/build_user_lib.sh "$lib_name"
			fi
		done
	fi

	for os_app in http ifconfig ping; do
		./build/scripts/build_user_app.sh "os/$os_app"
	done

	# Native apps replace script wrappers so binaries on disk are the real
	# implementations instead of .cmd redirect scripts.
	for os_app in http ifconfig ping; do
		if [ -f "artifacts/user/os/$os_app.bin" ]; then
			cp "artifacts/user/os/$os_app.bin" "artifacts/os/$os_app.bin"
		fi
	done

	./build/scripts/build_user_app.sh "vgahello"
	app_mappings+=("artifacts/user/vgahello.bin=/apps/vgahello.bin")

	./build/scripts/build_user_app.sh "draw"
	app_mappings+=("artifacts/user/draw.bin=/apps/draw.bin")

	./build/scripts/build_user_app.sh "win"
	app_mappings+=("artifacts/user/win.bin=/apps/win.bin")

	# Deploy root certificate to /certs for runtime signature validation
	CERTS_ARGS=""
	if [ -d "$ROOT_DIR/artifacts/keys" ] && [ -f "$ROOT_DIR/artifacts/keys/root.pub" ]; then
		mkdir -p "$ROOT_DIR/artifacts/certs"
		cp "$ROOT_DIR/artifacts/keys/root.pub" "$ROOT_DIR/artifacts/certs/root.pub"
		CERTS_ARGS="--certs-dir artifacts/certs"
	fi

	python3 tools/populate_disk_image.py "$DISK_PATH" "$DISK_BLOCKS" \
		--os-dir artifacts/os \
		--lib-dir artifacts/lib \
		$CERTS_ARGS \
		"${app_mappings[@]}"
	echo "Built $DISK_PATH"
}

build_disk_image_inner
echo "PASS: disk image build"
