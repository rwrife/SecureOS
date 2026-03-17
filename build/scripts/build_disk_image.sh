#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DISK_DIR="$ROOT_DIR/artifacts/disk"
DISK_PATH="$DISK_DIR/secureos-disk.img"
DISK_BLOCKS="${SECUREOS_DISK_BLOCKS:-4096}"
IMAGE_TAG="${SECUREOS_TOOLCHAIN_IMAGE:-secureos/toolchain:bookworm-2026-02-12}"

stop_secureos_instances() {
	if command -v docker >/dev/null 2>&1; then
		mapfile -t IDS < <(docker ps --filter "ancestor=$IMAGE_TAG" --format "{{.ID}}")
		if [[ ${#IDS[@]} -gt 0 ]]; then
			docker stop "${IDS[@]}" >/dev/null 2>&1 || true
		fi
	fi

	if command -v pkill >/dev/null 2>&1; then
		pkill -f "qemu-system-x86_64.*secureos-disk.img" >/dev/null 2>&1 || true
		pkill -f "qemu-system-x86_64.*secureos.iso" >/dev/null 2>&1 || true
	fi
}

stop_secureos_instances

build_disk_image_inner() {
	local script_path
	local cmd_name
	local lib_path
	local lib_name
	local os_app
	local -a app_mappings=()

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
		app_mappings+=("artifacts/user/os/$os_app.bin=/os/$os_app.bin")
	done

	python3 tools/populate_disk_image.py "$DISK_PATH" "$DISK_BLOCKS" \
		--os-dir artifacts/os \
		--lib-dir artifacts/lib \
		"${app_mappings[@]}"
	echo "Built $DISK_PATH"
}

if command -v docker >/dev/null 2>&1; then
	if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
		docker build -f "$ROOT_DIR/build/docker/Dockerfile.toolchain" -t "$IMAGE_TAG" "$ROOT_DIR"
	fi

	docker run --rm -v "$ROOT_DIR":/workspace -w /workspace "$IMAGE_TAG" \
		bash -lc 'set -euo pipefail; ./build/scripts/build_disk_image.sh'
else
	build_disk_image_inner
fi

echo "PASS: disk image build"
