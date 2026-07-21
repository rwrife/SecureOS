#!/usr/bin/env python3
"""Host drift gate for canonical /apps/dev/include header staging (issue #615).

Compares the authoritative header-set pin in
`tests/disk_image/apps_dev_include_set.json` against the staged destination set
declared by `build/scripts/build_disk_image.sh`.

Behavior:
- Any unexpected staged header under `/apps/dev/include/` is an immediate FAIL.
- Missing non-pending pinned headers are an immediate FAIL.
- Missing pending headers are tolerated (with canonical SKIP marker) while their
  gating issues are OPEN/UNKNOWN.
- Once a pending header's gating issue is CLOSED, that header becomes required.
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

PIN_REL = Path("tests/disk_image/apps_dev_include_set.json")
BUILD_SCRIPT_REL = Path("build/scripts/build_disk_image.sh")
INCLUDE_PREFIX = "/apps/dev/include/"
MAPPING_TOKEN_RE = re.compile(r"[\"']([^\"']+=/[^\"']+)[\"']")


def emit(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def gh_issue_state(issue: int, repo: str) -> tuple[str, str]:
    """Return (OPEN|CLOSED|UNKNOWN, detail)."""
    if not shutil.which("gh"):
        return ("UNKNOWN", "gh_not_found")
    try:
        proc = subprocess.run(
            ["gh", "api", f"repos/{repo}/issues/{issue}"],
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
        emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:missing_pin:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:malformed_pin:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err("APPS_DEV_INCLUDE_SET:FAIL:pin_not_object")
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
        emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:bad_schema:{pin.get('schemaVersion')!r}")
        return 1

    skip_marker = pin.get("skipMarker")
    if not isinstance(skip_marker, str) or not skip_marker.startswith("SKIP:#"):
        emit_err("APPS_DEV_INCLUDE_SET:FAIL:skip_marker_invalid")
        return 1

    headers = pin.get("headers")
    if not isinstance(headers, list) or not headers:
        emit_err("APPS_DEV_INCLUDE_SET:FAIL:headers_missing_or_empty")
        return 1

    pinned_paths: dict[str, dict[str, Any]] = {}
    declared_gating_issues = pin.get("gatingIssues")
    if declared_gating_issues is not None:
        if not isinstance(declared_gating_issues, list) or not all(
            isinstance(i, int) for i in declared_gating_issues
        ):
            emit_err("APPS_DEV_INCLUDE_SET:FAIL:gating_issues_invalid")
            return 1

    for idx, entry in enumerate(headers):
        if not isinstance(entry, dict):
            emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:header_entry_not_object:{idx}")
            return 1
        path = entry.get("path")
        if not isinstance(path, str) or not path.startswith(INCLUDE_PREFIX):
            emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:invalid_header_path:{idx}:{path!r}")
            return 1
        norm_path = path.rstrip("/")
        if norm_path in pinned_paths:
            emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:duplicate_header_path:{norm_path}")
            return 1

        pending = entry.get("pending", False)
        if not isinstance(pending, bool):
            emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:pending_not_bool:{norm_path}")
            return 1
        if pending:
            gating_issue = entry.get("gatingIssue")
            if not isinstance(gating_issue, int):
                emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:missing_gating_issue:{norm_path}")
                return 1

        pinned_paths[norm_path] = entry

    if not build_script_path.is_file():
        emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:missing_manifest_source:{build_script_path}")
        return 1

    staged_targets = parse_manifest_targets(build_script_path.read_text(encoding="utf-8"))
    staged_headers = sorted(p for p in staged_targets if p.startswith(INCLUDE_PREFIX))

    expected_header_paths = set(pinned_paths.keys())
    staged_header_paths = set(staged_headers)
    missing = sorted(expected_header_paths - staged_header_paths)
    extras = sorted(staged_header_paths - expected_header_paths)

    for path in sorted(expected_header_paths):
        if path in staged_header_paths:
            emit(f"APPS_DEV_INCLUDE_SET:PASS:present:{path}")
        else:
            emit(f"APPS_DEV_INCLUDE_SET:INFO:missing:{path}")

    had_failure = False
    pending_skip_applies = False

    for path in missing:
        entry = pinned_paths[path]
        pending = bool(entry.get("pending", False))
        if not pending:
            emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:missing_required_header:{path}")
            had_failure = True
            continue

        gating_issue = int(entry["gatingIssue"])
        state, detail = gh_issue_state(gating_issue, args.repo)
        emit(
            f"APPS_DEV_INCLUDE_SET:INFO:gating_issue:{gating_issue}:"
            f"state={state}:detail={detail}:path={path}"
        )

        if state == "CLOSED":
            emit_err(
                "APPS_DEV_INCLUDE_SET:FAIL:missing_pending_header_after_gate_closed:"
                f"{path}:issue={gating_issue}"
            )
            had_failure = True
        else:
            pending_skip_applies = True

    for path in extras:
        emit_err(f"APPS_DEV_INCLUDE_SET:FAIL:unexpected_staged_header:{path}")
        had_failure = True

    if had_failure:
        emit_err("APPS_DEV_INCLUDE_SET:FAIL:summary:header_set_drift")
        return 1

    if pending_skip_applies:
        emit(skip_marker)
        emit(f"APPS_DEV_INCLUDE_SET:SKIP:{skip_marker}")
        emit("APPS_DEV_INCLUDE_SET:PASS:summary:pending_headers_deferred")
        return 0

    emit("APPS_DEV_INCLUDE_SET:PASS:summary:all_expected_headers_present_and_no_extras")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
