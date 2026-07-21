#!/usr/bin/env python3
"""report_skip_backlog.py — issue #631.

Weekly report generator for TEST:SKIP backlog drift.

Outputs two artifacts under artifacts/reports/skip-backlog/ by default:
  - YYYY-Www.json
  - YYYY-Www.md

Design goals:
- deterministic body (stable sort order, no timestamp fields in body),
- lightweight GitHub issue-state lookup via `gh issue view`,
- pure reader (no writes outside configured output directory).
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path
from typing import Any

SKIP_RE = re.compile(r"TEST:SKIP:([a-zA-Z0-9_./-]+):([^\n\r]*)")
AWAITING_RE = re.compile(r"awaiting_(\d+)")


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def emit_out(line: str) -> None:
    print(line, flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root.",
    )
    parser.add_argument(
        "--tests-dir",
        default=None,
        help="Override tests directory (defaults to <root>/tests).",
    )
    parser.add_argument(
        "--markers",
        default=None,
        help="Override markers.json path (defaults to <root>/tests/m7_toolchain/markers.json).",
    )
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub owner/repo used for issue state lookups via gh.",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help=(
            "Directory where YYYY-Www.{json,md} will be written "
            "(defaults to <root>/artifacts/reports/skip-backlog)."
        ),
    )
    parser.add_argument(
        "--allow-offline",
        action="store_true",
        help="Skip gh issue-state lookups and emit an empty closed-issues list.",
    )
    parser.add_argument(
        "--report-week",
        default=None,
        help="Override week key (format YYYY-Www); default derives from current UTC week.",
    )
    parser.add_argument(
        "--generated-at",
        default=None,
        help="Override generated_at timestamp (ISO-8601 UTC) for deterministic fixtures.",
    )
    return parser.parse_args()


def week_key(now_utc: dt.datetime) -> str:
    y, w, _ = now_utc.isocalendar()
    return f"{y}-W{w:02d}"


def load_markers(markers_path: Path) -> list[dict[str, Any]]:
    if not markers_path.exists():
        emit_err(f"SKIP_BACKLOG:FAIL:missing_markers_json:{markers_path}")
        sys.exit(2)
    try:
        doc = json.loads(markers_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"SKIP_BACKLOG:FAIL:malformed_markers_json:{exc}")
        sys.exit(2)
    markers = doc.get("markers")
    if not isinstance(markers, list):
        emit_err("SKIP_BACKLOG:FAIL:invalid_markers_list")
        sys.exit(2)
    out: list[dict[str, Any]] = []
    for idx, row in enumerate(markers):
        if not isinstance(row, dict):
            emit_err(f"SKIP_BACKLOG:FAIL:invalid_marker_row:{idx}")
            sys.exit(2)
        out.append(row)
    return out


def scan_skip_markers(tests_dir: Path) -> list[dict[str, str]]:
    if not tests_dir.is_dir():
        emit_err(f"SKIP_BACKLOG:FAIL:missing_tests_dir:{tests_dir}")
        sys.exit(2)

    rows: list[dict[str, str]] = []
    for path in sorted(p for p in tests_dir.rglob("*") if p.is_file()):
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        rel = path.as_posix()
        for m in SKIP_RE.finditer(text):
            rows.append({"path": rel, "name": m.group(1), "reason": m.group(2).strip()})
    return rows


def sort_gating_keys(keys: set[str]) -> list[str]:
    numeric: list[tuple[int, str]] = []
    others: list[str] = []
    for key in keys:
        if key.startswith("#") and key[1:].isdigit():
            numeric.append((int(key[1:]), key))
        else:
            others.append(key)
    numeric.sort(key=lambda x: x[0])
    ordered = [k for _, k in numeric]
    ordered.extend(sorted(others))
    # Keep unpinned as the final bucket for readability.
    if "unpinned" in ordered:
        ordered = [k for k in ordered if k != "unpinned"] + ["unpinned"]
    return ordered


def gh_issue_state(issue_num: int, repo: str) -> str:
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
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return "UNKNOWN"
    state = str(data.get("state", "")).upper()
    if state in {"OPEN", "CLOSED"}:
        return state
    return "UNKNOWN"


def build_report(
    source_skips: list[dict[str, str]],
    markers: list[dict[str, Any]],
    repo: str,
    allow_offline: bool,
) -> dict[str, Any]:
    marker_to_issue: dict[str, str] = {}
    registry_by_issue: Counter[str] = Counter()

    for row in markers:
        name = row.get("name")
        gating = row.get("gatingIssue")
        key = "unpinned"
        if isinstance(gating, int):
            key = f"#{gating}"
        registry_by_issue[key] += 1
        if isinstance(name, str):
            marker_to_issue[name] = key

    source_by_issue: Counter[str] = Counter()
    for row in source_skips:
        name = row["name"]
        reason = row["reason"]
        if name in marker_to_issue:
            source_by_issue[marker_to_issue[name]] += 1
            continue
        m = AWAITING_RE.search(reason)
        if m:
            source_by_issue[f"#{m.group(1)}"] += 1
        else:
            source_by_issue["unpinned"] += 1

    keys = set(source_by_issue.keys()) | set(registry_by_issue.keys())
    ordered_keys = sort_gating_keys(keys)

    per_issue: dict[str, dict[str, int]] = {}
    for key in ordered_keys:
        per_issue[key] = {
            "source_skip_markers": int(source_by_issue.get(key, 0)),
            "registry_markers": int(registry_by_issue.get(key, 0)),
        }

    # Top-5 by source pinned count; tie-break by issue number asc.
    ranked: list[tuple[int, int, str]] = []
    for key, count in source_by_issue.items():
        if key == "unpinned" or count <= 0:
            continue
        issue_num = int(key[1:]) if key.startswith("#") and key[1:].isdigit() else 10**9
        ranked.append((-count, issue_num, key))
    ranked.sort()
    top5 = [{"gating_issue": key, "count": -neg} for neg, _, key in ranked[:5]]

    closed_issues: list[str] = []
    unknown_state_issues: list[str] = []
    if not allow_offline:
        for key in ordered_keys:
            if not key.startswith("#") or not key[1:].isdigit():
                continue
            state = gh_issue_state(int(key[1:]), repo)
            if state == "CLOSED":
                closed_issues.append(key)
            elif state == "UNKNOWN":
                unknown_state_issues.append(key)

    body = {
        "counts": {
            "source_skip_markers_total": len(source_skips),
            "registry_markers_total": len(markers),
        },
        "per_gating_issue": per_issue,
        "top5_gating_issues_by_pinned_harnesses": top5,
        "closed_gating_issues_referenced_by_skips": closed_issues,
        "unknown_state_gating_issues": unknown_state_issues,
    }
    return body


def render_markdown(header: dict[str, str], body: dict[str, Any]) -> str:
    counts = body["counts"]
    lines = [
        f"# SKIP Backlog Report ({header['report_week']})",
        "",
        f"> generated_at: {header['generated_at']}",
        f"> repo: {header['repo']}",
        "",
        f"- Source `TEST:SKIP:*` markers in `tests/**`: {counts['source_skip_markers_total']}",
        f"- Registry entries in `tests/m7_toolchain/markers.json`: {counts['registry_markers_total']}",
        "",
        "## Per-gating-issue breakdown",
        "",
        "| gating issue | source SKIPs | registry markers |",
        "|---|---:|---:|",
    ]

    for key, row in body["per_gating_issue"].items():
        lines.append(
            f"| {key} | {row['source_skip_markers']} | {row['registry_markers']} |"
        )

    lines.extend(
        [
            "",
            "## Top 5 gating issues by pinned harness count",
            "",
            "| gating issue | count |",
            "|---|---:|",
        ]
    )
    top5 = body["top5_gating_issues_by_pinned_harnesses"]
    if top5:
        for row in top5:
            lines.append(f"| {row['gating_issue']} | {row['count']} |")
    else:
        lines.append("| _(none)_ | 0 |")

    lines.extend(["", "## Closed gating issues referenced by SKIPs", ""])
    closed = body["closed_gating_issues_referenced_by_skips"]
    if closed:
        for key in closed:
            lines.append(f"- {key}")
    else:
        lines.append("- (none)")

    unknown = body["unknown_state_gating_issues"]
    if unknown:
        lines.extend(["", "## Gating issues with unknown state (gh unavailable)", ""])
        for key in unknown:
            lines.append(f"- {key}")

    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    tests_dir = Path(args.tests_dir).resolve() if args.tests_dir else (root / "tests")
    markers_path = (
        Path(args.markers).resolve()
        if args.markers
        else (root / "tests" / "m7_toolchain" / "markers.json")
    )
    output_dir = (
        Path(args.output_dir).resolve()
        if args.output_dir
        else (root / "artifacts" / "reports" / "skip-backlog")
    )

    now = dt.datetime.now(dt.timezone.utc)
    generated_at = (
        args.generated_at
        if args.generated_at
        else now.replace(microsecond=0).isoformat().replace("+00:00", "Z")
    )
    report_week = args.report_week if args.report_week else week_key(now)

    markers = load_markers(markers_path)
    source_skips = scan_skip_markers(tests_dir)
    body = build_report(source_skips, markers, args.repo, args.allow_offline)

    header = {
        "generated_at": generated_at,
        "report_week": report_week,
        "repo": args.repo,
    }

    payload = {
        "header": header,
        "body": body,
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / f"{report_week}.json"
    md_path = output_dir / f"{report_week}.md"

    json_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    md_path.write_text(render_markdown(header, body), encoding="utf-8")

    emit_out(f"SKIP_BACKLOG:PASS:json:{json_path}")
    emit_out(f"SKIP_BACKLOG:PASS:markdown:{md_path}")
    emit_out(f"SKIP_BACKLOG:PASS:source_skip_markers:{body['counts']['source_skip_markers_total']}")
    emit_out(f"SKIP_BACKLOG:PASS:registry_markers:{body['counts']['registry_markers_total']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
