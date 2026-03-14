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

mkdir -p "$DISK_DIR"

python3 - <<PY
from pathlib import Path
path = Path(r'''$DISK_PATH''')
blocks = int(r'''$DISK_BLOCKS''')
path.write_bytes(b'\x00' * (blocks * 512))
print(f"Built {path}")
PY

echo "PASS: disk image build"
