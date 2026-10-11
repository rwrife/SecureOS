#!/usr/bin/env python3
"""Regression check: unfinished developer-image gates follow DEMO-04, not closed predecessor issues.

Called by: build/scripts/test.sh apps_dev_staging (via the test harness).
"""
import contextlib
import importlib.util
import io
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
staging = json.loads((root / "tests/disk_image/apps_dev_manifest.json").read_text())
headers = json.loads((root / "tests/disk_image/apps_dev_include_set.json").read_text())
hashes = json.loads((root / "tools/disk_image_apps_dev_sha.json").read_text())
for pin in (staging, headers, hashes):
    assert pin["gatingIssues"] == [768], pin["gatingIssues"]
    assert pin["skipMarker"] == "SKIP:#768", pin["skipMarker"]
pending = [entry for entry in headers["headers"] if entry.get("pending")]
assert len(pending) == 1 and pending[0]["gatingIssue"] == 768, pending
# Exercise the real validators offline without modifying live issue states.
for name in (
    "validate_apps_dev_staging",
    "validate_apps_dev_include_set",
):
    spec = importlib.util.spec_from_file_location(name, root / f"tools/{name}.py")
    assert spec is not None and spec.loader is not None
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    for state in ("OPEN", "UNKNOWN", "CLOSED"):
        setattr(validator, "gh_issue_state", lambda issue, repo, st=state: (st, "fixture"))
        sys.argv = [name, "--root", str(root)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            result = validator.main()
        if state == "CLOSED":
            assert result == 1 and "FAIL:" in output.getvalue(), output.getvalue()
        else:
            assert "SKIP:#768" in output.getvalue(), output.getvalue()

print("TEST:PASS:dev_gate_owner")
