#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ART_DIR="$ROOT_DIR/artifacts/qemu"
QEMU_ARGS_FILE="$ROOT_DIR/build/qemu/x86_64-headless.args"
TEST_NAME=""
TIMEOUT_SECONDS="${SECUREOS_QEMU_TIMEOUT_SECONDS:-10}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --test <name>

Supported tests:
  hello_boot  Uses experiments/bootloader/boot.bin as floppy image

Outputs:
  artifacts/qemu/<test>.log
  artifacts/qemu/<test>.meta.json
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

if [[ ! -f "$QEMU_ARGS_FILE" ]]; then
  echo "Missing QEMU args file: $QEMU_ARGS_FILE"
  exit 1
fi

mkdir -p "$ART_DIR"
LOG_FILE="$ART_DIR/${TEST_NAME}.log"
META_FILE="$ART_DIR/${TEST_NAME}.meta.json"

case "$TEST_NAME" in
  hello_boot)
    BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
    if [[ ! -f "$BOOT_BIN" ]]; then
      echo "Missing boot binary: $BOOT_BIN"
      echo "Run build/scripts/test.sh hello_boot first."
      exit 1
    fi

    set +e
    python3 - <<PY
import json, pathlib, subprocess, shlex

root = pathlib.Path(r'''$ROOT_DIR''')
args_file = pathlib.Path(r'''$QEMU_ARGS_FILE''')
log_file = pathlib.Path(r'''$LOG_FILE''')
meta_file = pathlib.Path(r'''$META_FILE''')
boot_bin = pathlib.Path(r'''$BOOT_BIN''')

timeout_s = int(r'''$TIMEOUT_SECONDS''')

raw_args = [line.strip() for line in args_file.read_text().splitlines() if line.strip() and not line.strip().startswith('#')]
cmd = ["qemu-system-x86_64", "-drive", f"format=raw,file={boot_bin},if=floppy", *raw_args]

result = {
    "test": r'''$TEST_NAME''',
    "timeoutSeconds": timeout_s,
    "command": cmd,
    "logFile": str(log_file),
}

try:
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)
    output = (p.stdout or "") + (p.stderr or "")
    log_file.write_text(output)
    result["qemuExitCode"] = p.returncode
    result["timedOut"] = False
except subprocess.TimeoutExpired as e:
    out = (e.stdout or b"") + (e.stderr or b"")
    if isinstance(out, bytes):
        out = out.decode("utf-8", errors="replace")
    log_file.write_text(out)
    result["qemuExitCode"] = 124
    result["timedOut"] = True

log_text = log_file.read_text(errors="replace")
result["detectedMessage"] = "SecureOS boot sector OK" in log_text
result["status"] = "pass" if result["detectedMessage"] else "fail"
meta_file.write_text(json.dumps(result, indent=2) + "\n")

print(log_text, end="")
print(f"META:{meta_file}")
if result["status"] == "pass":
    print(f"QEMU_PASS:{result['test']}")
    raise SystemExit(0)

print(f"QEMU_FAIL:{result['test']}")
raise SystemExit(1)
PY
    EXIT_CODE=$?
    set -e
    exit $EXIT_CODE
    ;;
  *)
    echo "Unknown test: $TEST_NAME"
    exit 1
    ;;
esac
