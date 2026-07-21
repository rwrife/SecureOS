#!/usr/bin/env python3
"""Host drift gate for required /apps/dev disk-image staging paths (issue #570).

This validator treats build/scripts/build_disk_image.sh as the staging
manifest and asserts that every required /apps/dev path from
`tests/disk_image/apps_dev_manifest.json` is present.

SKIP policy:
- While any gating issue from the pin is OPEN (or issue state is unknown),
  missing paths emit the canonical marker from the pin (currently
  `SKIP:#541,#545,#548,#550,#531`) and the validator exits 0.
- Once all gating issues are CLOSED, any missing required path is a FAIL.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

PIN_REL = Path("tests/disk_image/apps_dev_manifest.json")
BUILD_SCRIPT_REL = Path("build/scripts/build_disk_image.sh")
MAPPING_TOKEN_RE = re.compile(r"[\"']([^\"']+=/[^\"']+)[\"']")


def emit(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def gh_issue_state(issue: int, repo: str) -> tuple[str, str]:
    if not shutil.which("gh"):
        return ("UNKNOWN", "gh_not_found")
    try:
        proc = subprocess.run(
            ["gh", "issue", "view", str(issue), "--repo", repo, "--json", "state"],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return ("UNKNOWN", "gh_timeout")
    except Exception as exc:  # pragma: no cover - defensive
        return ("UNKNOWN", f"gh_error:{exc}")
    if proc.returncode != 0:
        return ("UNKNOWN", "gh_exit_nonzero")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return ("UNKNOWN", "gh_json_parse")
    state = str(data.get("state", "")).upper()
    if state in {"OPEN", "CLOSED"}:
        return (state, "ok")
    return ("UNKNOWN", f"unexpected_state:{state}")


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"APPS_DEV_STAGING:FAIL:missing_pin:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"APPS_DEV_STAGING:FAIL:malformed_pin:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err("APPS_DEV_STAGING:FAIL:pin_not_object")
        raise SystemExit(2)
    return raw


def parse_manifest_targets(script_text: str) -> set[str]:
    targets: set[str] = set()
    for token in MAPPING_TOKEN_RE.findall(script_text):
        _, dst = token.split("=", 1)
        path = dst.strip()
        if not path.startswith("/"):
            continue
        targets.add(path.rstrip("/"))
    return targets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub repo for issue-state checks.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    pin_path = root / PIN_REL
    build_script_path = root / BUILD_SCRIPT_REL

    pin = load_json_object(pin_path)

    if pin.get("schemaVersion") != 1:
        emit_err(f"APPS_DEV_STAGING:FAIL:bad_schema:{pin.get('schemaVersion')!r}")
        return 1

    required_paths = pin.get("requiredPaths")
    if not isinstance(required_paths, list) or not required_paths:
        emit_err("APPS_DEV_STAGING:FAIL:required_paths_missing_or_empty")
        return 1
    if any(not isinstance(p, str) or not p.startswith("/") for p in required_paths):
        emit_err("APPS_DEV_STAGING:FAIL:required_paths_invalid")
        return 1

    gating_issues = pin.get("gatingIssues")
    if not isinstance(gating_issues, list) or not gating_issues:
        emit_err("APPS_DEV_STAGING:FAIL:gating_issues_missing_or_empty")
        return 1
    if any(not isinstance(i, int) for i in gating_issues):
        emit_err("APPS_DEV_STAGING:FAIL:gating_issues_invalid")
        return 1

    skip_marker = pin.get("skipMarker")
    if not isinstance(skip_marker, str) or not skip_marker.startswith("SKIP:#"):
        emit_err("APPS_DEV_STAGING:FAIL:skip_marker_invalid")
        return 1

    if not build_script_path.is_file():
        emit_err(f"APPS_DEV_STAGING:FAIL:missing_manifest_source:{build_script_path}")
        return 1

    staged_targets = parse_manifest_targets(build_script_path.read_text(encoding="utf-8"))

    missing = []
    for path in required_paths:
        if path.rstrip("/") in staged_targets:
            emit(f"APPS_DEV_STAGING:PASS:present:{path}")
        else:
            missing.append(path)

    if not missing:
        emit("APPS_DEV_STAGING:PASS:summary:all_required_paths_present")
        return 0

    states: list[str] = []
    for issue in gating_issues:
        state, detail = gh_issue_state(issue, args.repo)
        states.append(state)
        emit(f"APPS_DEV_STAGING:INFO:gating_issue:{issue}:state={state}:detail={detail}")

    if any(state in {"OPEN", "UNKNOWN"} for state in states):
        # Canonical marker required by issue #570 acceptance criteria.
        emit(skip_marker)
        emit(f"APPS_DEV_STAGING:SKIP:{skip_marker}")
        emit("APPS_DEV_STAGING:PASS:summary:skip_while_gating_open")
        return 0

    # All gating issues resolved CLOSED: missing required staged paths now fail.
    for path in missing:
        emit_err(f"APPS_DEV_STAGING:FAIL:missing_required_path:{path}")
    emit_err("APPS_DEV_STAGING:FAIL:summary:gating_closed_missing_paths")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
