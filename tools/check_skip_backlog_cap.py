#!/usr/bin/env python3
# tools/check_skip_backlog_cap.py
#
# Issue #641: cap the number of SKIP-pinned M7 harness markers that can
# accumulate behind any single OPEN gating issue.
#
# Source of truth:
#   tests/m7_toolchain/markers.json
#
# Policy:
#   - default cap = 12 markers per OPEN gating issue
#   - legacy overages are grandfathered through
#     tests/m7_toolchain/skip_backlog_cap_allowlist.json
#   - allowlist is remove-only: entries may be removed as counts drop, but
#     new entries should not be added for fresh overages
#
# Exit codes:
#   0  cap check passed
#   1  cap violation / policy failure
#   2  usage / input error

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any

MARKER = "SKIP_BACKLOG_CAP"


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Fail when any OPEN gating issue in tests/m7_toolchain/markers.json "
            "exceeds the configured SKIP-pinned harness cap."
        )
    )
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--markers",
        default=None,
        help="Override markers.json path (defaults to <root>/tests/m7_toolchain/markers.json).",
    )
    parser.add_argument(
        "--allowlist",
        default=None,
        help=(
            "Override allowlist path (defaults to "
            "<root>/tests/m7_toolchain/skip_backlog_cap_allowlist.json)."
        ),
    )
    parser.add_argument(
        "--cap",
        type=int,
        default=12,
        help="Per-open-gating-issue cap (default: 12).",
    )
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub owner/repo for gh-backed issue state checks.",
    )
    parser.add_argument(
        "--allow-offline",
        action="store_true",
        help="Skip gh lookups; unknown issue states emit SKIP markers instead of failing.",
    )
    parser.add_argument(
        "--issue-states-json",
        default=None,
        help=(
            "Optional JSON map of issue number -> OPEN/CLOSED/UNKNOWN used for "
            "deterministic fixture tests. When set, gh lookups are bypassed."
        ),
    )
    args = parser.parse_args()
    if args.cap < 1:
        emit_err(f"{MARKER}:FAIL:invalid_cap:{args.cap}")
        sys.exit(2)
    return args


def load_markers(markers_path: Path) -> list[dict[str, Any]]:
    if not markers_path.exists():
        emit_err(f"{MARKER}:FAIL:missing_markers_json:{markers_path}")
        sys.exit(2)
    try:
        doc = json.loads(markers_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"{MARKER}:FAIL:malformed_markers_json:{exc}")
        sys.exit(2)

    markers = doc.get("markers")
    if not isinstance(markers, list):
        emit_err(f"{MARKER}:FAIL:invalid_markers_list")
        sys.exit(2)

    rows: list[dict[str, Any]] = []
    for idx, row in enumerate(markers):
        if not isinstance(row, dict):
            emit_err(f"{MARKER}:FAIL:invalid_marker_row:{idx}")
            sys.exit(2)
        rows.append(row)
    return rows


def load_allowlist(allowlist_path: Path, cap: int) -> dict[int, int]:
    if not allowlist_path.exists():
        emit_err(f"{MARKER}:FAIL:missing_allowlist:{allowlist_path}")
        sys.exit(2)

    try:
        doc = json.loads(allowlist_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"{MARKER}:FAIL:malformed_allowlist_json:{exc}")
        sys.exit(2)

    if not isinstance(doc, dict):
        emit_err(f"{MARKER}:FAIL:invalid_allowlist_shape")
        sys.exit(2)

    schema_version = doc.get("schemaVersion")
    if schema_version != 1:
        emit_err(f"{MARKER}:FAIL:unsupported_allowlist_schema:{schema_version!r}")
        sys.exit(2)

    declared_cap = doc.get("defaultCap")
    if declared_cap is not None and declared_cap != cap:
        emit_err(
            f"{MARKER}:FAIL:allowlist_cap_mismatch:allowlist={declared_cap}:runtime={cap}"
        )
        sys.exit(1)

    if doc.get("removeOnly") is not True:
        emit_err(f"{MARKER}:FAIL:allowlist_remove_only_flag_missing")
        sys.exit(2)

    raw = doc.get("grandfatheredOverages")
    if not isinstance(raw, list):
        emit_err(f"{MARKER}:FAIL:invalid_allowlist_entries")
        sys.exit(2)

    parsed: dict[int, int] = {}
    for idx, row in enumerate(raw):
        if not isinstance(row, dict):
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_row:{idx}")
            sys.exit(2)
        issue = row.get("gatingIssue")
        ceiling = row.get("maxPinnedHarnesses")
        if not isinstance(issue, int) or issue <= 0:
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_issue:{idx}:{issue!r}")
            sys.exit(2)
        if not isinstance(ceiling, int) or ceiling <= 0:
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_ceiling:{idx}:{ceiling!r}")
            sys.exit(2)
        if issue in parsed:
            emit_err(f"{MARKER}:FAIL:duplicate_allowlist_issue:{issue}")
            sys.exit(2)
        parsed[issue] = ceiling

    return parsed


def load_issue_state_overrides(path: Path) -> dict[int, str]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"{MARKER}:FAIL:missing_issue_states_json:{path}")
        sys.exit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"{MARKER}:FAIL:malformed_issue_states_json:{exc}")
        sys.exit(2)

    if not isinstance(doc, dict):
        emit_err(f"{MARKER}:FAIL:invalid_issue_states_shape")
        sys.exit(2)

    out: dict[int, str] = {}
    for k, v in doc.items():
        try:
            issue = int(k)
        except (TypeError, ValueError):
            emit_err(f"{MARKER}:FAIL:invalid_issue_state_key:{k!r}")
            sys.exit(2)
        state = str(v).upper()
        if state not in {"OPEN", "CLOSED", "UNKNOWN"}:
            emit_err(f"{MARKER}:FAIL:invalid_issue_state_value:{k}:{v!r}")
            sys.exit(2)
        out[issue] = state
    return out


def gh_issue_state(issue_num: int, repo: str) -> str:
    if not shutil.which("gh"):
        return "UNKNOWN"
    try:
        proc = subprocess.run(
            ["gh", "issue", "view", str(issue_num), "--repo", repo, "--json", "state"],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except Exception:
        return "UNKNOWN"
    if proc.returncode != 0:
        return "UNKNOWN"
    try:
        payload = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return "UNKNOWN"
    state = str(payload.get("state", "")).upper()
    if state in {"OPEN", "CLOSED"}:
        return state
    return "UNKNOWN"


def count_markers_by_issue(markers: list[dict[str, Any]]) -> Counter[int]:
    counts: Counter[int] = Counter()
    for idx, row in enumerate(markers):
        issue = row.get("gatingIssue")
        if issue is None:
            continue
        if not isinstance(issue, int) or issue <= 0:
            emit_err(f"{MARKER}:FAIL:invalid_gating_issue:{idx}:{issue!r}")
            sys.exit(1)
        counts[issue] += 1
    return counts


def main() -> int:
    args = parse_args()

    root = Path(args.root).resolve()
    markers_path = (
        Path(args.markers).resolve()
        if args.markers
        else (root / "tests" / "m7_toolchain" / "markers.json")
    )
    allowlist_path = (
        Path(args.allowlist).resolve()
        if args.allowlist
        else (root / "tests" / "m7_toolchain" / "skip_backlog_cap_allowlist.json")
    )

    markers = load_markers(markers_path)
    counts = count_markers_by_issue(markers)
    allowlist = load_allowlist(allowlist_path, args.cap)

    override_states: dict[int, str] | None = None
    if args.issue_states_json:
        override_states = load_issue_state_overrides(Path(args.issue_states_json).resolve())

    issue_state_cache: dict[int, str] = {}

    def state_for(issue: int) -> str:
        cached = issue_state_cache.get(issue)
        if cached:
            return cached
        if override_states is not None:
            state = override_states.get(issue, "UNKNOWN")
        elif args.allow_offline:
            state = "UNKNOWN"
        else:
            state = gh_issue_state(issue, args.repo)
        issue_state_cache[issue] = state
        return state

    failures = 0
    checked_open = 0
    grandfathered_open = 0

    all_issues = sorted(set(counts.keys()) | set(allowlist.keys()))
    for issue in all_issues:
        count = int(counts.get(issue, 0))
        state = state_for(issue)
        tag = f"#{issue}"

        if issue in allowlist:
            ceiling = allowlist[issue]
            if count <= args.cap:
                emit_err(
                    f"{MARKER}:FAIL:{tag}:stale_allowlist_entry:count={count}:cap={args.cap}"
                )
                failures += 1
            if count > ceiling:
                emit_err(
                    f"{MARKER}:FAIL:{tag}:grandfathered_ceiling_exceeded:count={count}:allowlist={ceiling}"
                )
                failures += 1
            if state == "CLOSED":
                emit_err(f"{MARKER}:FAIL:{tag}:allowlisted_issue_closed_remove_entry")
                failures += 1
            elif state == "OPEN":
                checked_open += 1
                grandfathered_open += 1
                emit_out(
                    f"{MARKER}:PASS:{tag}:grandfathered_open_over_cap:count={count}:cap={args.cap}:allowlist={ceiling}"
                )
            else:
                emit_out(
                    f"{MARKER}:SKIP:{tag}:allowlisted_state_unknown:count={count}:cap={args.cap}"
                )
            continue

        if state == "OPEN":
            checked_open += 1
            if count > args.cap:
                emit_err(
                    f"{MARKER}:FAIL:{tag}:open_issue_over_cap_without_allowlist:count={count}:cap={args.cap}"
                )
                failures += 1
            else:
                emit_out(
                    f"{MARKER}:PASS:{tag}:open_issue_within_cap:count={count}:cap={args.cap}"
                )
        elif state == "CLOSED":
            emit_out(f"{MARKER}:PASS:{tag}:closed_issue_ignored:count={count}")
        else:
            emit_out(f"{MARKER}:SKIP:{tag}:issue_state_unknown:count={count}:cap={args.cap}")

    emit_out(
        f"{MARKER}:PASS:counts:tracked_markers={sum(counts.values())}:unique_gating_issues={len(counts)}"
    )
    emit_out(
        f"{MARKER}:PASS:policy:cap={args.cap}:allowlist_entries={len(allowlist)}:checked_open_issues={checked_open}:grandfathered_open_issues={grandfathered_open}"
    )

    if failures:
        emit_err(f"{MARKER}:FAIL:summary:{failures}_violations")
        return 1

    emit_out(f"{MARKER}:PASS:summary:no_open_issue_exceeds_cap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
