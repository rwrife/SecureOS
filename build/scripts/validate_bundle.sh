#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STARTED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
RUN_ID="${SECUREOS_RUN_ID:-$(date -u +"%Y%m%dT%H%M%SZ")-$(git -C "$ROOT_DIR" rev-parse --short HEAD)}"
RUN_DIR="$ROOT_DIR/artifacts/runs/$RUN_ID"
RUN_QEMU_DIR="$RUN_DIR/qemu"
RUN_TESTS_DIR="$RUN_DIR/tests"

mkdir -p "$RUN_QEMU_DIR" "$RUN_TESTS_DIR"

TEST_TARGETS=(
  hello_boot
  hello_boot_negative
  cap_api_contract
  capability_table
  capability_gate
)

STATUS_LINES=()
FAILED_TESTS=()

for target in "${TEST_TARGETS[@]}"; do
  test_started="$(date +%s)"
  if "$ROOT_DIR/build/scripts/test.sh" "$target"; then
    status="pass"
  else
    status="fail"
    FAILED_TESTS+=("$target")
  fi
  test_finished="$(date +%s)"
  duration="$((test_finished - test_started))"
  STATUS_LINES+=("${target}|${status}|${duration}")
done

# Copy known QEMU artifacts when present.
for qemu_name in hello_boot hello_boot_fail; do
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" "$RUN_QEMU_DIR/"
  fi
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" "$RUN_QEMU_DIR/"
  fi
done

if [[ -d "$ROOT_DIR/artifacts/tests" ]]; then
  find "$ROOT_DIR/artifacts/tests" -maxdepth 1 -type f -exec cp {} "$RUN_TESTS_DIR/" \;
fi

BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
IMAGE_HASH=""
if [[ -f "$BOOT_BIN" ]]; then
  IMAGE_HASH="$(shasum -a 256 "$BOOT_BIN" | awk '{print $1}')"
fi

GIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD)"
GIT_REF="$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD)"
FINISHED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

STATUS_LINES_JSON="$(printf '%s\n' "${STATUS_LINES[@]}")"
export ROOT_DIR RUN_DIR RUN_ID STARTED_AT FINISHED_AT GIT_SHA GIT_REF IMAGE_HASH STATUS_LINES_JSON

python3 - <<'PY'
import json
import os
from pathlib import Path

root_dir = Path(os.environ["ROOT_DIR"])
run_dir = Path(os.environ["RUN_DIR"])
status_lines = [line for line in os.environ.get("STATUS_LINES_JSON", "").splitlines() if line.strip()]

checks = []
failed = []
for line in status_lines:
    name, status, duration = line.split("|", 2)
    checks.append({
        "name": name,
        "status": status,
        "pass": status == "pass",
        "durationSeconds": int(duration),
    })
    if status != "pass":
        failed.append(name)

qemu_meta = run_dir / "qemu" / "hello_boot.meta.json"
qemu_command = []
if qemu_meta.exists():
    try:
        qemu_command = json.loads(qemu_meta.read_text()).get("command", [])
    except Exception:
        qemu_command = []

build_metadata = {
    "runId": os.environ["RUN_ID"],
    "startedAt": os.environ["STARTED_AT"],
    "finishedAt": os.environ["FINISHED_AT"],
    "git": {
        "sha": os.environ["GIT_SHA"],
        "ref": os.environ["GIT_REF"],
    },
    "artifactsRoot": str(run_dir),
}
(run_dir / "build_metadata.json").write_text(json.dumps(build_metadata, indent=2) + "\n")

report = {
    "schemaVersion": 1,
    "runId": os.environ["RUN_ID"],
    "startedAt": os.environ["STARTED_AT"],
    "finishedAt": os.environ["FINISHED_AT"],
    "overallStatus": "pass" if not failed else "fail",
    "pass": len(failed) == 0,
    "failedChecks": failed,
    "checks": checks,
    "image": {
      "path": "experiments/bootloader/boot.bin",
      "sha256": os.environ.get("IMAGE_HASH", ""),
    },
    "qemu": {
      "command": qemu_command,
      "serialLogs": [
        "qemu/hello_boot.log",
        "qemu/hello_boot_fail.log"
      ]
    }
}
(run_dir / "validator_report.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"VALIDATION_REPORT:{run_dir / 'validator_report.json'}")
PY

if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
  echo "VALIDATION_FAIL:${FAILED_TESTS[*]}"
  exit 1
fi

echo "VALIDATION_PASS:$RUN_DIR/validator_report.json"