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
  harness_negative
  cap_api_contract
  capability_table
  capability_gate
  capability_audit
  capability_audit_fixture
  capability_audit_log
  cap_broker
  cap_deny_marker_shape
  bearssl_compile
  broker_share_allow
  broker_share_deny
  broker_share_revoke
    event_bus
    scheduler
    sof_format
    sof_verify_at_rest
    tls
    https
    fs_service
    fs_service_persist_allow
    fs_service_persist_deny
    fs_service_ephemeral_reset
    app_runtime
    helloapp_allow
    helloapp_deny
    kernel_console
    kernel_filedemo
    kernel_persistence
    validator_report
    ipc_sync_v0
    ipc_port_lifecycle
    ipc_handle_gate
    proc_sched
    m1_ipc_demo
    validate_abi_stamps
    # M4 capability-broker substrate (umbrella #299, plan
    # plans/2026-05-25-m4-broker-on-m1-substrate.md): host-side broker_svc
    # checks + the three `_qemu` peers (slices 003/004) are all green on
    # main but had not been wired into the bundle gate. Adding them so a
    # future M4 substrate regression flips the bundle to FAIL.
    broker_svc_port_alloc
    broker_svc_delete_owner_authority_check
    broker_svc_cascade_revokes_minted_handle
    m4_broker_share_allow_qemu
    m4_broker_share_deny_qemu
    m4_broker_share_revoke_qemu
    # M5 ownership-graph cascade (umbrella #313, plan
    # plans/2026-05-25-m5-ownership-on-m1-substrate.md): allow-side
    # `_qemu` peer (slice 003). The deny-side peer
    # (`m5_owner_delete_cascade_deny_qemu`, #326 / PR #362) lands
    # alongside its source PR.
    m5_owner_delete_cascade_allow_qemu
    validate_sosh_capability_contract
)
# NOTE: ed25519, cert_chain, codesign, and kernel_sessions are intentionally
# NOT in TEST_TARGETS yet — see issue #129. They are wired into test.sh /
# test.ps1 but currently red (ed25519 sign/verify roundtrip, cert_chain root
# validation, codesign -Werror=unused-variable on tests/codesign_test.c:307)
# or blocked on the disk-image perms chain (#106 / PR #107 for kernel_sessions).
# Add each here as the corresponding fix lands, so the bundle stays green.

# Issue #212: targets that MUST fail. The bundle's pass condition is
# "every TEST_TARGETS target passes AND every EXPECTED_FAIL_TARGETS
# target fails". Each entry here is a permanent canary that proves the
# harness still classifies a deliberate failure as a failure (defends
# against #90 / #129 / #140 style silent no-ops).
EXPECTED_FAIL_TARGETS=(
  canary_must_fail
)

STATUS_LINES=()
FAILED_TESTS=()

# Exit code 78 is reserved by build/scripts/test.sh for HARNESS_ERROR
# (missing/unreadable subordinate script). It is reported as a distinct
# `harness_error` status so agents can tell infra breakage from a real
# regression (see issue #91).
HARNESS_ERROR_EXIT=78

for target in "${TEST_TARGETS[@]}"; do
  test_started="$(date +%s)"
  set +e
  bash "$ROOT_DIR/build/scripts/test.sh" "$target"
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    status="pass"
  elif [[ $rc -eq $HARNESS_ERROR_EXIT ]]; then
    status="harness_error"
    FAILED_TESTS+=("$target")
  else
    status="fail"
    FAILED_TESTS+=("$target")
  fi
  test_finished="$(date +%s)"
  duration="$((test_finished - test_started))"
  STATUS_LINES+=("${target}|${status}|${duration}|expected_pass")
done

# Issue #212: expected-fail canaries. We INVERT the pass condition: a
# canary that *fails* (rc=1) is the green path and is recorded as
# status=pass with expectedFail=true / observed=fail / classification=ok
# in the JSON report. A canary that unexpectedly *passes* (rc=0) flips
# the bundle to FAIL with the deterministic marker
# `BUNDLE_FAIL: canary did not fail`. A harness error (rc=78) is still
# reported as harness_error so infra breakage stays distinguishable.
for target in "${EXPECTED_FAIL_TARGETS[@]}"; do
  test_started="$(date +%s)"
  set +e
  bash "$ROOT_DIR/build/scripts/test.sh" "$target"
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    # Canary did NOT fail -- this is the regression we exist to catch.
    status="fail"
    observed="pass"
    classification="anomaly"
    FAILED_TESTS+=("$target")
    echo "BUNDLE_FAIL: canary did not fail" >&2
    echo "BUNDLE_FAIL: canary did not fail (target=$target)"
  elif [[ $rc -eq $HARNESS_ERROR_EXIT ]]; then
    status="harness_error"
    observed="harness_error"
    classification="harness_error"
    FAILED_TESTS+=("$target")
  else
    # Expected failure observed -- this is the green path for canaries.
    status="pass"
    observed="fail"
    classification="ok"
  fi
  test_finished="$(date +%s)"
  duration="$((test_finished - test_started))"
  STATUS_LINES+=("${target}|${status}|${duration}|expected_fail|${observed}|${classification}")
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
targets = []
failed = []
harness_errors = []

def _resolve_log_and_artifacts(target_name: str):
    """Return (logPath, artifacts[]) relative to run_dir for a given target.

    Probes the conventional artifact locations populated earlier in this
    script (qemu/ and tests/). Returns (None, []) if nothing matched.
    """
    qemu_aliases = {
        "hello_boot":          ["hello_boot"],
        "hello_boot_negative": ["hello_boot_fail"],
        "kernel_console":      ["kernel_console"],
        "kernel_filedemo":     ["kernel_filedemo"],
        "kernel_persistence":  ["kernel_persistence"],
    }
    artifacts_rel = []
    log_rel = None
    for stem in qemu_aliases.get(target_name, []):
        log = run_dir / "qemu" / f"{stem}.log"
        if log.exists():
            rel = str(log.relative_to(run_dir))
            artifacts_rel.append(rel)
            if log_rel is None:
                log_rel = rel
        meta = run_dir / "qemu" / f"{stem}.meta.json"
        if meta.exists():
            artifacts_rel.append(str(meta.relative_to(run_dir)))
    candidates = [
        run_dir / "tests" / f"{target_name}.log",
        run_dir / "tests" / f"{target_name}_test.log",
    ]
    for cand in candidates:
        if cand.exists():
            rel = str(cand.relative_to(run_dir))
            artifacts_rel.append(rel)
            if log_rel is None:
                log_rel = rel
    return log_rel, artifacts_rel

for line in status_lines:
    parts = line.split("|")
    # New format (issue #212): name|status|duration|kind[|observed|classification]
    # Legacy format: name|status|duration
    if len(parts) >= 4:
        name = parts[0]
        status = parts[1]
        duration = parts[2]
        kind = parts[3]
        observed = parts[4] if len(parts) > 4 else None
        classification = parts[5] if len(parts) > 5 else None
    else:
        name, status, duration = parts[0], parts[1], parts[2]
        kind = "expected_pass"
        observed = None
        classification = None
    if status not in ("pass", "fail", "harness_error"):
        # Defensive: treat unknown statuses as fail rather than crash the JSON.
        status = "fail"
    check_entry = {
        "name": name,
        "status": status,
        "pass": status == "pass",
        "durationSeconds": int(duration),
    }
    if kind == "expected_fail":
        check_entry["expectedFail"] = True
        if observed is not None:
            check_entry["observed"] = observed
        if classification is not None:
            check_entry["classification"] = classification
    checks.append(check_entry)
    log_path, artifacts_for_target = _resolve_log_and_artifacts(name)
    target_entry = {
        "name": name,
        "status": status,
        "pass": status == "pass",
        "durationSeconds": int(duration),
        "logPath": log_path,
        "reasonCode": None if status == "pass" else status,
        "artifacts": artifacts_for_target,
    }
    if kind == "expected_fail":
        target_entry["expectedFail"] = True
        if observed is not None:
            target_entry["observed"] = observed
        if classification is not None:
            target_entry["classification"] = classification
    targets.append(target_entry)
    if status == "harness_error":
        harness_errors.append(name)
        failed.append(name)
    elif status != "pass":
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

_summary_check_status = "pass" if summary_check_error is None else "fail"
checks.append({
    "name": "capability_audit_summary_contract",
    "status": _summary_check_status,
    "pass": summary_check_error is None,
    "durationSeconds": 0,
    "details": {
        "summaryPath": str(summary_path.relative_to(run_dir)),
    },
})
targets.append({
    "name": "capability_audit_summary_contract",
    "status": _summary_check_status,
    "pass": summary_check_error is None,
    "durationSeconds": 0,
    "logPath": str(summary_path.relative_to(run_dir)) if summary_path.exists() else None,
    "reasonCode": summary_check_error,
    "artifacts": [str(summary_path.relative_to(run_dir))] if summary_path.exists() else [],
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

summary_total = len(targets)
summary_passed = sum(1 for t in targets if t["status"] == "pass")
summary_failed = sum(1 for t in targets if t["status"] == "fail")
summary_harness = sum(1 for t in targets if t["status"] == "harness_error")

report = {
    "schemaVersion": 1,
    "runId": os.environ["RUN_ID"],
    "startedAt": os.environ["STARTED_AT"],
    "finishedAt": os.environ["FINISHED_AT"],
    "git": {
        "sha": os.environ["GIT_SHA"],
        "ref": os.environ["GIT_REF"],
    },
    "overallStatus": "pass" if not failed else "fail",
    "pass": len(failed) == 0,
    "failedChecks": failed,
    "harnessErrors": harness_errors,
    "summary": {
        "total":         summary_total,
        "passed":        summary_passed,
        "failed":        summary_failed,
        "harnessErrors": summary_harness,
    },
    "targets": targets,
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
report_path = run_dir / "validator_report.json"
report_path.write_text(json.dumps(report, indent=2) + "\n")
print(f"VALIDATION_REPORT:{report_path}")

# Lightweight, dependency-free structural validation against
# docs/test-plans/validator-report.schema.json. Full JSON-Schema
# validation is best-effort: if `jsonschema` is importable we use it,
# otherwise we fall back to a minimal hand-rolled check that mirrors
# the schema's `required` and enum constraints so a green run still
# proves the file is parseable and well-shaped.
schema_path = root_dir / "docs" / "test-plans" / "validator-report.schema.json"
schema_error = None
if not schema_path.exists():
    schema_error = f"missing schema: {schema_path}"
else:
    try:
        schema = json.loads(schema_path.read_text())
    except Exception as exc:
        schema_error = f"unreadable schema: {exc}"
    if schema_error is None:
        try:
            import jsonschema  # type: ignore
            jsonschema.validate(report, schema)
        except ImportError:
            required = schema.get("required", [])
            missing = [k for k in required if k not in report]
            if missing:
                schema_error = f"missing required top-level fields: {missing}"
            elif report.get("schemaVersion") != 1:
                schema_error = "schemaVersion must be 1"
            elif report.get("overallStatus") not in ("pass", "fail"):
                schema_error = "overallStatus must be pass|fail"
            else:
                for t in report.get("targets", []):
                    if t.get("status") not in ("pass", "fail", "harness_error"):
                        schema_error = f"target {t.get('name')} has invalid status"
                        break
                    for k in ("name", "status", "durationSeconds"):
                        if k not in t:
                            schema_error = f"target missing required field: {k}"
                            break
                    if schema_error:
                        break
                if schema_error is None:
                    s = report.get("summary", {})
                    for k in ("total", "passed", "failed", "harnessErrors"):
                        if not isinstance(s.get(k), int):
                            schema_error = f"summary.{k} must be integer"
                            break
        except Exception as exc:  # jsonschema.ValidationError or similar
            schema_error = f"schema validation failed: {exc}"

if schema_error is not None:
    print(f"VALIDATION_SCHEMA_FAIL:{schema_error}")
    raise SystemExit(3)
print(f"VALIDATION_SCHEMA_OK:{schema_path.relative_to(root_dir)}")
if summary_check_error is not None:
    print(f"VALIDATION_CONTRACT_FAIL:{summary_check_error}")
    raise SystemExit(2)
PY

if [[ ${#FAILED_TESTS[@]} -gt 0 ]]; then
  echo "VALIDATION_FAIL:${FAILED_TESTS[*]}"
  exit 1
fi

echo "VALIDATION_PASS:$RUN_DIR/validator_report.json"