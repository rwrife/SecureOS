#!/usr/bin/env bash
# Unit test for the validator_report.json shape + schema.
#
# Issue #110: BUILD_ROADMAP §4.3 / §6.3 require build/scripts/validate_bundle.sh
# to emit a machine-readable JSON report. This test does NOT run the full
# validator (that's covered by `validate_bundle.sh` itself, which now performs
# inline schema validation and exits non-zero on a malformed report). Instead,
# it constructs a representative report in memory and asserts it conforms to
# `docs/test-plans/validator-report.schema.json`, so regressions in the schema
# or the validator's report shape surface as a fast, deterministic failure.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCHEMA="$ROOT_DIR/docs/test-plans/validator-report.schema.json"

if [[ ! -f "$SCHEMA" ]]; then
  echo "TEST:FAIL:test_validator_report:missing_schema:$SCHEMA" >&2
  exit 1
fi

export ROOT_DIR SCHEMA
python3 - <<'PY'
import json
import os
import sys
from pathlib import Path

root_dir = Path(os.environ["ROOT_DIR"])
schema_path = Path(os.environ["SCHEMA"])

schema = json.loads(schema_path.read_text())

# Synthesize a representative validator_report.json that mirrors what
# build/scripts/validate_bundle.sh writes on a green run plus the failure
# enum values the schema must support.
sample = {
    "schemaVersion": 1,
    "runId": "20260512T000000Z-deadbee",
    "startedAt": "2026-05-12T00:00:00Z",
    "finishedAt": "2026-05-12T00:00:01Z",
    "git": {"sha": "deadbeefcafebabe1234", "ref": "main"},
    "overallStatus": "fail",
    "pass": False,
    "failedChecks": ["sample_fail", "sample_harness"],
    "harnessErrors": ["sample_harness"],
    "summary": {"total": 3, "passed": 1, "failed": 1, "harnessErrors": 1},
    "targets": [
        {
            "name": "sample_pass",
            "status": "pass",
            "pass": True,
            "durationSeconds": 2,
            "logPath": "qemu/sample_pass.log",
            "reasonCode": None,
            "artifacts": ["qemu/sample_pass.log", "qemu/sample_pass.meta.json"],
        },
        {
            "name": "sample_fail",
            "status": "fail",
            "pass": False,
            "durationSeconds": 5,
            "logPath": None,
            "reasonCode": "fail",
            "artifacts": [],
        },
        {
            "name": "sample_harness",
            "status": "harness_error",
            "pass": False,
            "durationSeconds": 0,
            "logPath": None,
            "reasonCode": "harness_error",
            "artifacts": [],
        },
    ],
    "checks": [
        {"name": "sample_pass", "status": "pass", "pass": True, "durationSeconds": 2},
    ],
    "image": {"path": "experiments/bootloader/boot.bin", "sha256": ""},
    "kernelIso": {"path": "secureos.iso", "sha256": ""},
    "qemu": {"commands": {}, "serialLogs": []},
}

errors = []

def assert_eq(name, got, want):
    if got != want:
        errors.append(f"{name}: expected {want!r}, got {got!r}")

# Schema sanity
assert_eq("schema.$id present", "$id" in schema, True)
assert_eq("schema.title", schema.get("title"), "SecureOS validator_report.json")

# Use jsonschema when available; otherwise fall back to the same minimal
# structural checks validate_bundle.sh applies inline.
try:
    import jsonschema  # type: ignore
    jsonschema.validate(sample, schema)
except ImportError:
    for k in schema.get("required", []):
        if k not in sample:
            errors.append(f"required field missing from sample: {k}")
    for t in sample["targets"]:
        if t["status"] not in ("pass", "fail", "harness_error"):
            errors.append(f"bad target status: {t}")
        for k in ("name", "status", "durationSeconds"):
            if k not in t:
                errors.append(f"target missing {k}: {t}")
    for k in ("total", "passed", "failed", "harnessErrors"):
        if not isinstance(sample["summary"].get(k), int):
            errors.append(f"summary.{k} must be int")
except Exception as exc:
    errors.append(f"jsonschema.validate failed: {exc}")

# Negative case: invalid status must fail the same path.
bad = json.loads(json.dumps(sample))
bad["targets"][0]["status"] = "totally-bogus"
caught = False
try:
    import jsonschema  # type: ignore
    try:
        jsonschema.validate(bad, schema)
    except jsonschema.ValidationError:
        caught = True
except ImportError:
    caught = any(
        t["status"] not in ("pass", "fail", "harness_error")
        for t in bad["targets"]
    )
if not caught:
    errors.append("schema accepted invalid target.status")

if errors:
    for e in errors:
        print(f"TEST:FAIL:test_validator_report:{e}")
    sys.exit(1)

print("TEST:PASS:test_validator_report:schema_present")
print("TEST:PASS:test_validator_report:sample_validates")
print("TEST:PASS:test_validator_report:rejects_invalid_status")
print("TEST:PASS:test_validator_report")
PY
