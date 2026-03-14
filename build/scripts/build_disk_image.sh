#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DISK_DIR="$ROOT_DIR/artifacts/disk"
DISK_PATH="$DISK_DIR/secureos-disk.img"
DISK_BLOCKS="${SECUREOS_DISK_BLOCKS:-4096}"

mkdir -p "$DISK_DIR"

python3 - <<PY
from pathlib import Path
path = Path(r'''$DISK_PATH''')
blocks = int(r'''$DISK_BLOCKS''')
path.write_bytes(b'\x00' * (blocks * 512))
print(f"Built {path}")
PY

echo "PASS: disk image build"
