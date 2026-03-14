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
  capability_audit
    event_bus
    fs_service
    app_runtime
    kernel_console
    kernel_filedemo
    kernel_persistence
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
for qemu_name in hello_boot hello_boot_fail kernel_console kernel_filedemo kernel_persistence; do
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.log" "$RUN_QEMU_DIR/"
  fi
  if [[ -f "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" ]]; then
    cp "$ROOT_DIR/artifacts/qemu/${qemu_name}.meta.json" "$RUN_QEMU_DIR/"
  fi
done

if [[ -f "$ROOT_DIR/artifacts/kernel/secureos.iso" ]]; then
    cp "$ROOT_DIR/artifacts/kernel/secureos.iso" "$RUN_DIR/"
fi

if [[ -d "$ROOT_DIR/artifacts/tests" ]]; then
  find "$ROOT_DIR/artifacts/tests" -maxdepth 1 -type f -exec cp {} "$RUN_TESTS_DIR/" \;
fi

BOOT_BIN="$ROOT_DIR/experiments/bootloader/boot.bin"
KERNEL_ISO="$ROOT_DIR/artifacts/kernel/secureos.iso"
IMAGE_HASH=""
if [[ -f "$BOOT_BIN" ]]; then
  IMAGE_HASH="$(shasum -a 256 "$BOOT_BIN" | awk '{print $1}')"
fi

KERNEL_ISO_HASH=""
if [[ -f "$KERNEL_ISO" ]]; then
    KERNEL_ISO_HASH="$(shasum -a 256 "$KERNEL_ISO" | awk '{print $1}')"
fi

GIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD)"
GIT_REF="$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD)"
FINISHED_AT="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

STATUS_LINES_JSON="$(printf '%s\n' "${STATUS_LINES[@]}")"
export ROOT_DIR RUN_DIR RUN_ID STARTED_AT FINISHED_AT GIT_SHA GIT_REF IMAGE_HASH KERNEL_ISO_HASH STATUS_LINES_JSON

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

qemu_commands = {}
for meta_name in ["hello_boot", "kernel_console", "kernel_filedemo", "kernel_persistence"]:
    qemu_meta = run_dir / "qemu" / f"{meta_name}.meta.json"
    if not qemu_meta.exists():
        continue
    try:
        qemu_commands[meta_name] = json.loads(qemu_meta.read_text()).get("command", [])
    except Exception:
        qemu_commands[meta_name] = []

summary_check_error = None
summary_path = run_dir / "tests" / "capability_audit_summary.json"
summary = None

if not summary_path.exists():
    summary_check_error = f"missing summary artifact: {summary_path}"
else:
    try:
        summary = json.loads(summary_path.read_text())
    except Exception as exc:
        summary_check_error = f"invalid JSON in {summary_path}: {exc}"

if summary is not None:
    required_int_fields = [
        "schemaVersion",
        "ringCapacity",
        "retainedEvents",
        "droppedEvents",
        "checkpointCount",
        "latestCheckpointId",
        "latestCheckpointSeal",
        "latestCheckpointDroppedCount",
    ]
    for field in required_int_fields:
        value = summary.get(field)
        if not isinstance(value, int):
            summary_check_error = f"{field} must be integer"
            break

    sequence_window = summary.get("sequenceWindow") if isinstance(summary, dict) else None
    checkpoint_window = summary.get("checkpointWindow") if isinstance(summary, dict) else None

    if summary_check_error is None and not isinstance(sequence_window, dict):
        summary_check_error = "sequenceWindow must be object"
    if summary_check_error is None and not isinstance(checkpoint_window, dict):
        summary_check_error = "checkpointWindow must be object"

    if summary_check_error is None:
        for field in ["firstSequenceId", "lastSequenceId", "eventCount"]:
            if not isinstance(sequence_window.get(field), int):
                summary_check_error = f"sequenceWindow.{field} must be integer"
                break

    if summary_check_error is None:
        if sequence_window.get("coverage") not in ["full", "truncated"]:
            summary_check_error = "sequenceWindow.coverage must be full or truncated"

    if summary_check_error is None:
        for field in ["firstCheckpointId", "lastCheckpointId", "count"]:
            if not isinstance(checkpoint_window.get(field), int):
                summary_check_error = f"checkpointWindow.{field} must be integer"
                break

    if summary_check_error is None:
        if summary.get("schemaVersion") != 1:
            summary_check_error = "schemaVersion must be 1"
        elif summary.get("test") != "capability_audit":
            summary_check_error = "test must be capability_audit"
        elif summary.get("ringCapacity", 0) <= 0:
            summary_check_error = "ringCapacity must be > 0"
        elif summary.get("retainedEvents", -1) < 0:
            summary_check_error = "retainedEvents must be >= 0"
        elif summary.get("droppedEvents", -1) < 0:
            summary_check_error = "droppedEvents must be >= 0"
        elif summary.get("retainedEvents", 0) > summary.get("ringCapacity", 0):
            summary_check_error = "retainedEvents must be <= ringCapacity"
        elif summary.get("retainedEvents", 0) + summary.get("droppedEvents", 0) < summary.get("ringCapacity", 0):
            summary_check_error = "retainedEvents + droppedEvents must be >= ringCapacity"
        elif summary.get("checkpointCount", -1) < 0:
            summary_check_error = "checkpointCount must be >= 0"
        elif summary.get("latestCheckpointId", -1) < 0:
            summary_check_error = "latestCheckpointId must be >= 0"
        elif summary.get("latestCheckpointSeal", -1) < 0:
            summary_check_error = "latestCheckpointSeal must be >= 0"
        elif summary.get("latestCheckpointDroppedCount", -1) < 0:
            summary_check_error = "latestCheckpointDroppedCount must be >= 0"
        elif summary.get("checkpointCount", 0) == 0 and (
            summary.get("latestCheckpointId", 0) != 0
            or summary.get("latestCheckpointSeal", 0) != 0
            or summary.get("latestCheckpointDroppedCount", 0) != 0
        ):
            summary_check_error = "latest checkpoint fields must be 0 when checkpointCount is 0"
        elif sequence_window.get("eventCount") != summary.get("retainedEvents"):
            summary_check_error = "sequenceWindow.eventCount must match retainedEvents"
        elif sequence_window.get("coverage") == "truncated" and summary.get("droppedEvents", 0) == 0:
            summary_check_error = "sequenceWindow.coverage truncated requires droppedEvents > 0"
        elif sequence_window.get("coverage") == "full" and summary.get("droppedEvents", 0) > 0:
            summary_check_error = "sequenceWindow.coverage full requires droppedEvents == 0"
        elif checkpoint_window.get("count") != summary.get("checkpointCount"):
            summary_check_error = "checkpointWindow.count must match checkpointCount"
        elif summary.get("retainedEvents", 0) == 0 and (
            sequence_window.get("firstSequenceId", 0) != 0
            or sequence_window.get("lastSequenceId", 0) != 0
        ):
            summary_check_error = "sequenceWindow IDs must be 0 when retainedEvents is 0"
        elif summary.get("retainedEvents", 0) > 0 and sequence_window.get("firstSequenceId") > sequence_window.get("lastSequenceId"):
            summary_check_error = "sequenceWindow.firstSequenceId must be <= lastSequenceId"
        elif summary.get("checkpointCount", 0) == 0 and (
            checkpoint_window.get("firstCheckpointId", 0) != 0
            or checkpoint_window.get("lastCheckpointId", 0) != 0
        ):
            summary_check_error = "checkpointWindow IDs must be 0 when checkpointCount is 0"
        elif summary.get("checkpointCount", 0) > 0 and checkpoint_window.get("firstCheckpointId") > checkpoint_window.get("lastCheckpointId"):
            summary_check_error = "checkpointWindow.firstCheckpointId must be <= lastCheckpointId"

checks.append({
    "name": "capability_audit_summary_contract",
    "status": "pass" if summary_check_error is None else "fail",
    "pass": summary_check_error is None,
    "durationSeconds": 0,
    "details": {
        "summaryPath": str(summary_path.relative_to(run_dir)),
    },
})

if summary_check_error is not None:
    failed.append("capability_audit_summary_contract")

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
        "kernelIso": {
            "path": "secureos.iso",
            "sha256": os.environ.get("KERNEL_ISO_HASH", ""),
        },
    "qemu": {
            "commands": qemu_commands,
      "serialLogs": [
        "qemu/hello_boot.log",
                "qemu/hello_boot_fail.log",
                "qemu/kernel_console.log",
                "qemu/kernel_filedemo.log",
                "qemu/kernel_persistence.log"
      ]
    }
}
(run_dir / "validator_report.json").write_text(json.dumps(report, indent=2) + "\n")
print(f"VALIDATION_REPORT:{run_dir / 'validator_report.json'}")
if summary_check_error is not None:
    print(f"VALIDATION_CONTRACT_FAIL:{summary_check_error}")
    raise SystemExit(2)
PY

if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
  echo "VALIDATION_FAIL:${FAILED_TESTS[*]}"
  exit 1
fi

echo "VALIDATION_PASS:$RUN_DIR/validator_report.json"