#!/usr/bin/env python3
"""Host drift gate for /apps/dev staged artifact SHA pins (issue #606).

Compares `tools/disk_image_apps_dev_sha.json` against mappings declared in
`build/scripts/build_disk_image.sh`.

Checks:
- staged target set parity (missing/extra targets fail)
- target->source mapping parity (source drift fails)
- SHA-256 parity for non-pending entries whose source files exist
- pending entries may defer missing source files while gating issues are OPEN
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

PIN_REL = Path("tools/disk_image_apps_dev_sha.json")
BUILD_SCRIPT_REL = Path("build/scripts/build_disk_image.sh")
TARGET_PREFIX = "/apps/dev/"
MAPPING_TOKEN_RE = re.compile(r"[\"']([^\"']+=/[^\"']+)[\"']")


def emit(msg: str) -> None:
    print(msg, flush=True)


def emit_err(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


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
        emit_err(f"APPS_DEV_SHA:FAIL:missing_pin:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"APPS_DEV_SHA:FAIL:malformed_pin:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err("APPS_DEV_SHA:FAIL:pin_not_object")
        raise SystemExit(2)
    return raw


def parse_manifest_mappings(script_text: str) -> dict[str, str]:
    mappings: dict[str, str] = {}
    for token in MAPPING_TOKEN_RE.findall(script_text):
        src, dst = token.split("=", 1)
        target = dst.strip().rstrip("/")
        source = src.strip()
        if not target.startswith(TARGET_PREFIX):
            continue
        mappings[target] = source
    return mappings


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
    parser.add_argument(
        "--pin",
        default=str(PIN_REL),
        help="Path to pin JSON (relative to --root when not absolute).",
    )
    parser.add_argument(
        "--manifest",
        default=str(BUILD_SCRIPT_REL),
        help="Path to build manifest script (relative to --root when not absolute).",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    pin_path = Path(args.pin)
    build_script_path = Path(args.manifest)
    if not pin_path.is_absolute():
        pin_path = root / pin_path
    if not build_script_path.is_absolute():
        build_script_path = root / build_script_path

    pin = load_json_object(pin_path)

    if pin.get("schemaVersion") != 1:
        emit_err(f"APPS_DEV_SHA:FAIL:bad_schema:{pin.get('schemaVersion')!r}")
        return 1

    skip_marker = pin.get("skipMarker")
    if not isinstance(skip_marker, str) or not skip_marker.startswith("SKIP:#"):
        emit_err("APPS_DEV_SHA:FAIL:skip_marker_invalid")
        return 1

    entries = pin.get("entries")
    if not isinstance(entries, list) or not entries:
        emit_err("APPS_DEV_SHA:FAIL:entries_missing_or_empty")
        return 1

    if not build_script_path.is_file():
        emit_err(f"APPS_DEV_SHA:FAIL:missing_manifest_source:{build_script_path}")
        return 1

    staged_mappings = parse_manifest_mappings(build_script_path.read_text(encoding="utf-8"))

    pinned: dict[str, dict[str, Any]] = {}
    for idx, entry in enumerate(entries):
        if not isinstance(entry, dict):
            emit_err(f"APPS_DEV_SHA:FAIL:entry_not_object:{idx}")
            return 1

        target = entry.get("target")
        source = entry.get("source")
        origin = entry.get("origin")
        if not isinstance(target, str) or not target.startswith(TARGET_PREFIX):
            emit_err(f"APPS_DEV_SHA:FAIL:bad_target:{idx}:{target!r}")
            return 1
        if not isinstance(source, str) or not source:
            emit_err(f"APPS_DEV_SHA:FAIL:bad_source:{idx}:{source!r}")
            return 1
        if not isinstance(origin, str) or not origin:
            emit_err(f"APPS_DEV_SHA:FAIL:bad_origin:{idx}:{origin!r}")
            return 1

        key = target.rstrip("/")
        if key in pinned:
            emit_err(f"APPS_DEV_SHA:FAIL:duplicate_target:{key}")
            return 1

        pending = bool(entry.get("pending", False))
        if pending and not isinstance(entry.get("gatingIssue"), int):
            emit_err(f"APPS_DEV_SHA:FAIL:pending_missing_gating_issue:{key}")
            return 1

        pinned[key] = entry

    pinned_targets = set(pinned.keys())
    staged_targets = set(staged_mappings.keys())

    had_failure = False
    pending_skip_applies = False

    for target in sorted(pinned_targets - staged_targets):
        emit_err(f"APPS_DEV_SHA:FAIL:missing_staged_target:{target}")
        had_failure = True

    for target in sorted(staged_targets - pinned_targets):
        emit_err(f"APPS_DEV_SHA:FAIL:unexpected_staged_target:{target}")
        had_failure = True

    for target in sorted(pinned_targets & staged_targets):
        entry = pinned[target]
        expected_source = str(entry["source"]).strip()
        actual_source = staged_mappings[target]
        if expected_source != actual_source:
            emit_err(
                f"APPS_DEV_SHA:FAIL:source_mismatch:{target}:expected={expected_source}:actual={actual_source}"
            )
            had_failure = True
            continue

        source_path = root / expected_source
        pending = bool(entry.get("pending", False))
        expected_sha = entry.get("sha256")

        if not source_path.is_file():
            if not pending:
                emit_err(f"APPS_DEV_SHA:FAIL:missing_source_file:{expected_source}:target={target}")
                had_failure = True
                continue

            gating_issue = int(entry["gatingIssue"])
            state, detail = gh_issue_state(gating_issue, args.repo)
            emit(
                f"APPS_DEV_SHA:INFO:gating_issue:{gating_issue}:state={state}:detail={detail}:target={target}"
            )
            if state == "CLOSED":
                emit_err(
                    f"APPS_DEV_SHA:FAIL:pending_source_missing_after_gate_closed:{expected_source}:issue={gating_issue}:target={target}"
                )
                had_failure = True
            else:
                pending_skip_applies = True
            continue

        actual_sha = sha256_file(source_path)
        if not isinstance(expected_sha, str) or len(expected_sha) != 64:
            emit_err(f"APPS_DEV_SHA:FAIL:missing_or_bad_sha_pin:{target}")
            had_failure = True
            continue

        if actual_sha != expected_sha:
            emit_err(
                f"APPS_DEV_SHA:FAIL:sha_mismatch:{target}:expected={expected_sha}:actual={actual_sha}"
            )
            had_failure = True
            continue

        emit(f"APPS_DEV_SHA:PASS:{target}:{actual_sha}")

    if had_failure:
        emit_err("APPS_DEV_SHA:FAIL:summary:drift_detected")
        return 1

    if pending_skip_applies:
        emit(skip_marker)
        emit(f"APPS_DEV_SHA:SKIP:{skip_marker}")
        emit("APPS_DEV_SHA:PASS:summary:pending_sources_deferred")
        return 0

    emit("APPS_DEV_SHA:PASS:summary:all_sources_match_pin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
