#!/usr/bin/env python3
# tools/validate_m7_markers.py
#
# Drift gate for the M7-TOOLCHAIN acceptance suite (issue #494,
# scaffold #423, umbrella #403).
#
# `tests/m7_toolchain/markers.json` is the single source-of-truth list
# that ties each acceptance marker to the open issue whose landing
# flips its SKIP to PASS. This validator turns that JSON file into a
# contract instead of a comment block, mirroring the discipline of
# tools/validate_capability_registry.py (#234), tools/validate_abi_stamps.py
# (#297), and tools/validate_sosh_capability_contract.py (#351).
#
# Checks (each FAIL emits a stable M7_MARKER:FAIL:<reason> line):
#   1. Every marker.name appears as a `toolchain_*)` (or grouped
#      `name1|name2|...)`) case arm in build/scripts/test.sh and is
#      present in build/scripts/validate_bundle.sh's TEST_TARGETS block.
#   2. Every `toolchain_*` case arm in test.sh / TEST_TARGETS entry in
#      validate_bundle.sh is present in markers.json (orphan-from-
#      TEST_TARGETS shape — #129 / #366 / #401 / #414 / #469 / #482 / #487).
#   3. Each tests/m7_toolchain/<marker>.sh exists and contains the
#      literal `TEST:PASS:<marker>` string (so a rename in the stub
#      that breaks bundle rollup cannot land silently).
#   4. For each gatingIssue, resolve issue state from either:
#        - online gh query (`gh issue view`) when available, or
#        - offline cache (`tests/m7_toolchain/issue_state.cache.json`).
#      If the resolved issue is `CLOSED` while the marker reason is still
#      `awaiting_<n>`, FAIL with an actionable message. Unknown state only
#      SKIPs when `--allow-offline` is explicitly passed.
#
# Emits stable markers to stdout (PASS / SKIP) and stderr (FAIL) so the
# lint stage can classify failures without parsing English text.
#
# Exit codes:
#   0  every check passed
#   1  one or more M7_MARKER:FAIL markers emitted
#   2  environment / usage error (missing input file, malformed JSON)

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

CASE_ARM_RE = re.compile(r"^\s*([a-z][a-z0-9_|]*)\)\s*$")
# Comments inside the TEST_TARGETS block use leading `#`; targets are
# bare identifiers, one per line.
TARGET_LINE_RE = re.compile(r"^\s*([a-z][a-z0-9_]*)\s*$")
GH_ISSUE_RE = re.compile(r"^awaiting_(\d+)$")


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def load_markers(markers_path: Path) -> dict:
    if not markers_path.exists():
        emit_err(f"M7_MARKER:FAIL:missing_markers_json:{markers_path}")
        sys.exit(2)
    try:
        return json.loads(markers_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"M7_MARKER:FAIL:malformed_markers_json:{exc}")
        sys.exit(2)


def parse_test_sh_toolchain_arms(test_sh_path: Path) -> set[str]:
    """Return the set of `toolchain_*` names that appear as case arms.

    Handles both the single-arm form (`name)`) and the grouped form
    (`name1|name2|...)` used by the current scaffolding.
    """
    if not test_sh_path.exists():
        emit_err(f"M7_MARKER:FAIL:missing_test_sh:{test_sh_path}")
        sys.exit(2)
    arms: set[str] = set()
    in_case = False
    for line in test_sh_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped.startswith('case "$TEST_NAME"'):
            in_case = True
            continue
        if not in_case:
            continue
        if stripped == "esac":
            in_case = False
            continue
        m = CASE_ARM_RE.match(line)
        if not m:
            continue
        label = m.group(1)
        for name in label.split("|"):
            if name.startswith("toolchain_"):
                arms.add(name)
    return arms


def parse_validate_bundle_targets(validate_bundle_path: Path) -> set[str]:
    """Return the set of `toolchain_*` names listed in TEST_TARGETS=(...)."""
    if not validate_bundle_path.exists():
        emit_err(f"M7_MARKER:FAIL:missing_validate_bundle_sh:{validate_bundle_path}")
        sys.exit(2)
    targets: set[str] = set()
    in_block = False
    for line in validate_bundle_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not in_block:
            # Match `TEST_TARGETS=(` (allow whitespace + trailing comments).
            if re.match(r"^\s*TEST_TARGETS=\(", line):
                in_block = True
            continue
        # End of array.
        if stripped.startswith(")"):
            in_block = False
            continue
        if not stripped or stripped.startswith("#"):
            continue
        m = TARGET_LINE_RE.match(line)
        if not m:
            continue
        name = m.group(1)
        if name.startswith("toolchain_"):
            targets.add(name)
    return targets


def check_stub_contains_pass(tests_dir: Path, marker: str) -> str | None:
    """Return error reason if stub script is missing or lacks TEST:PASS:<marker>."""
    stub = tests_dir / f"{marker}.sh"
    if not stub.exists():
        return f"missing_stub_script:{stub}"
    text = stub.read_text(encoding="utf-8")
    if f"TEST:PASS:{marker}" not in text:
        return f"stub_missing_pass_marker:{stub}"
    return None


def load_issue_state_cache(cache_path: Path) -> dict[str, dict]:
    """Load offline gating-issue state cache.

    Expected shape:
      {
        "schemaVersion": 1,
        "issues": {
          "409": {"state": "OPEN"},
          "634": {"state": "CLOSED", "replacedBy": 409}
        }
      }
    """
    if not cache_path.exists():
        return {}
    try:
        raw = json.loads(cache_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"M7_MARKER:FAIL:malformed_issue_state_cache:{exc}")
        sys.exit(2)

    issues = raw.get("issues")
    if not isinstance(issues, dict):
        emit_err("M7_MARKER:FAIL:invalid_issue_state_cache:issues_not_object")
        sys.exit(2)

    normalized: dict[str, dict] = {}
    for key, value in issues.items():
        if not isinstance(value, dict):
            continue
        state = str(value.get("state", "")).upper()
        if state not in {"OPEN", "CLOSED"}:
            continue
        record: dict[str, object] = {"state": state}
        replaced = value.get("replacedBy")
        if isinstance(replaced, int):
            record["replacedBy"] = replaced
        normalized[str(key)] = record
    return normalized


def check_gating_issue_state_online(num: int, repo: str) -> tuple[str, str]:
    """Return ("OPEN"|"CLOSED"|"UNKNOWN", detail) using gh CLI."""
    if not shutil.which("gh"):
        return ("UNKNOWN", "gh_not_found")
    try:
        proc = subprocess.run(
            ["gh", "issue", "view", str(num), "--repo", repo, "--json", "state"],
            capture_output=True,
            text=True,
            timeout=20,
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
    if state in ("OPEN", "CLOSED"):
        return (state, "ok")
    return ("UNKNOWN", f"unexpected_state:{state}")


def resolve_gating_issue_state(
    num: int,
    repo: str,
    issue_cache: dict[str, dict],
    online_lookup: bool,
) -> tuple[str, str, int | None]:
    """Resolve issue state via online gh first, then offline cache.

    Returns (state, detail, replaced_by):
      - state: OPEN | CLOSED | UNKNOWN
      - detail: source-qualified detail marker
      - replaced_by: optional successor issue id from cache
    """
    if online_lookup:
        state, detail = check_gating_issue_state_online(num, repo)
        if state in {"OPEN", "CLOSED"}:
            return (state, f"online:{detail}", None)
        online_detail = detail
    else:
        online_detail = "online_disabled"

    cached = issue_cache.get(str(num))
    if isinstance(cached, dict):
        state = str(cached.get("state", "")).upper()
        if state in {"OPEN", "CLOSED"}:
            replaced = cached.get("replacedBy")
            replaced_by = replaced if isinstance(replaced, int) else None
            return (state, "cache:hit", replaced_by)

    return ("UNKNOWN", f"online_and_cache_miss:{online_detail}", None)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Drift gate for tests/m7_toolchain/markers.json."
    )
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--allow-offline",
        action="store_true",
        help=(
            "If issue state cannot be resolved from online gh lookup or the "
            "offline cache, emit SKIP markers instead of FAIL."
        ),
    )
    parser.add_argument(
        "--issue-state-cache",
        default="tests/m7_toolchain/issue_state.cache.json",
        help=(
            "Path (relative to --root or absolute) to offline issue-state "
            "cache JSON."
        ),
    )
    parser.add_argument(
        "--offline-cache-only",
        action="store_true",
        help="Disable online gh lookups and resolve gating issue states from cache only.",
    )
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub repo for gh issue lookups.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    markers_path = root / "tests" / "m7_toolchain" / "markers.json"
    tests_dir = root / "tests" / "m7_toolchain"
    test_sh_path = root / "build" / "scripts" / "test.sh"
    bundle_sh_path = root / "build" / "scripts" / "validate_bundle.sh"
    cache_path_arg = Path(args.issue_state_cache)
    issue_cache_path = (
        cache_path_arg if cache_path_arg.is_absolute() else root / cache_path_arg
    )
    issue_cache = load_issue_state_cache(issue_cache_path)

    doc = load_markers(markers_path)
    markers = doc.get("markers", [])
    if not isinstance(markers, list) or not markers:
        emit_err("M7_MARKER:FAIL:markers_list_empty_or_invalid")
        return 2

    json_names = []
    for entry in markers:
        name = entry.get("name")
        if not isinstance(name, str) or not name.startswith("toolchain_"):
            emit_err(f"M7_MARKER:FAIL:invalid_marker_name:{name!r}")
            return 1
        json_names.append(name)
    json_set = set(json_names)

    test_arms = parse_test_sh_toolchain_arms(test_sh_path)
    bundle_targets = parse_validate_bundle_targets(bundle_sh_path)

    failures = 0

    # 1a. markers.json -> test.sh
    for name in sorted(json_set - test_arms):
        emit_err(f"M7_MARKER:FAIL:{name}:missing_from_test_sh")
        failures += 1
    # 1b. markers.json -> validate_bundle.sh TEST_TARGETS
    for name in sorted(json_set - bundle_targets):
        emit_err(f"M7_MARKER:FAIL:{name}:missing_from_validate_bundle_targets")
        failures += 1
    # 2a. test.sh -> markers.json (orphan arm)
    for name in sorted(test_arms - json_set):
        emit_err(f"M7_MARKER:FAIL:{name}:orphan_test_sh_arm")
        failures += 1
    # 2b. validate_bundle.sh -> markers.json (orphan target)
    for name in sorted(bundle_targets - json_set):
        emit_err(f"M7_MARKER:FAIL:{name}:orphan_validate_bundle_target")
        failures += 1

    # 3. stub scripts must exist and emit TEST:PASS:<marker>.
    for name in json_names:
        reason = check_stub_contains_pass(tests_dir, name)
        if reason:
            emit_err(f"M7_MARKER:FAIL:{name}:{reason}")
            failures += 1
        else:
            emit_out(f"M7_MARKER:PASS:{name}:stub_pass_marker_present")

    # 4. gating issue state (online gh + offline cache fallback).
    online_lookup = not args.offline_cache_only
    for entry in markers:
        name = entry["name"]
        gating = entry.get("gatingIssue")
        reason = entry.get("reason", "")
        if not isinstance(gating, int):
            emit_err(f"M7_MARKER:FAIL:{name}:invalid_gating_issue:{gating!r}")
            failures += 1
            continue

        state, detail, replaced_by = resolve_gating_issue_state(
            gating,
            args.repo,
            issue_cache,
            online_lookup,
        )
        if state == "UNKNOWN":
            if args.allow_offline:
                emit_out(f"M7_MARKER:SKIP:{name}:gating_issue_unknown:{detail}")
            else:
                emit_err(f"M7_MARKER:FAIL:{name}:gating_issue_state_unknown:{detail}")
                failures += 1
            continue

        if state == "OPEN":
            emit_out(f"M7_MARKER:PASS:{name}:gating_issue_open:{gating}:{detail}")
            continue

        # CLOSED
        m = GH_ISSUE_RE.match(reason)
        if m and int(m.group(1)) == gating:
            suffix = (
                f":replaced_by:{replaced_by}" if isinstance(replaced_by, int) else ""
            )
            emit_err(
                f"M7_MARKER:FAIL:{name}:gating_issue_closed_but_reason_still_awaiting:{gating}:{detail}{suffix}"
            )
            failures += 1
        else:
            # Issue is closed but reason has already been retargeted
            # (or never matched the awaiting_<n> shape) — allowed.
            emit_out(
                f"M7_MARKER:PASS:{name}:gating_issue_closed_reason_retargeted:{gating}:{detail}"
            )

    if failures:
        emit_err(f"M7_MARKER:FAIL:summary:{failures}_failures")
        return 1
    emit_out(f"M7_MARKER:PASS:summary:{len(json_names)}_markers_validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
