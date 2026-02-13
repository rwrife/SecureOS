#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="$ROOT_DIR/artifacts/qemu"
TEST_NAME=""
TIMEOUT_SECONDS="${SECUREOS_QEMU_TIMEOUT_SECONDS:-10}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --test <name>

Supported tests:
  hello_boot  Uses experiments/bootloader/boot.bin as floppy image
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --test)
      TEST_NAME="${2:-}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ -z "$TEST_NAME" ]]; then
  usage
  exit 1
fi

mkdir -p "$ART_DIR"
LOG_FILE="$ART_DIR/${TEST_NAME}.log"

case "$TEST_NAME" in
  hello_boot)
    BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
    if [[ ! -f "$BOOT_BIN" ]]; then
      echo "Missing boot binary: $BOOT_BIN"
      echo "Run build/scripts/test.sh hello_boot first."
      exit 1
    fi

    set +e
    python3 - <<PY >"$LOG_FILE" 2>&1
import subprocess
cmd = [
  "qemu-system-x86_64",
  "-drive", "format=raw,file=$BOOT_BIN,if=floppy",
  "-nographic", "-serial", "stdio", "-monitor", "none",
  "-m", "256M", "-smp", "1",
]
try:
  p = subprocess.run(cmd, capture_output=True, text=True, timeout=$TIMEOUT_SECONDS)
  print((p.stdout or "") + (p.stderr or ""), end="")
  raise SystemExit(p.returncode)
except subprocess.TimeoutExpired as e:
  out = (e.stdout or b"") + (e.stderr or b"")
  if isinstance(out, bytes):
      out = out.decode("utf-8", errors="replace")
  print(out, end="")
  raise SystemExit(124)
PY
    QEMU_EXIT=$?
    set -e
    cat "$LOG_FILE"

    if [[ $QEMU_EXIT -ne 0 && $QEMU_EXIT -ne 124 ]]; then
      echo "QEMU failed with exit code $QEMU_EXIT"
      exit 1
    fi

    if grep -q "SecureOS boot sector OK" "$LOG_FILE"; then
      echo "QEMU_PASS:$TEST_NAME"
      exit 0
    fi

    echo "QEMU_FAIL:$TEST_NAME"
    exit 1
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    exit 1
    ;;
esac
