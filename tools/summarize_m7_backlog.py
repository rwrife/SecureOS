#!/usr/bin/env python3
"""summarize_m7_backlog.py — issue #627.

Build a per-gating-issue index for SKIP-pinned M7 harness backlog.

Reads:
- tests/m7_toolchain/markers.json
- open GitHub issues from `gh api` (or a fixture via --issues-file)

Writes:
- artifacts/m7-backlog/summary-<YYYY-MM-DD>.json

The script is intentionally read-only except for the output file under
artifacts/m7-backlog/.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

TARGET_GATING_ISSUES = [408, 409, 410, 421, 424, 522, 368, 585, 404, 406]


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root.",
    )
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub owner/repo for `gh api` lookups.",
    )
    parser.add_argument(
        "--markers",
        default=None,
        help="Override markers.json path (defaults to <root>/tests/m7_toolchain/markers.json).",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Output directory (defaults to <root>/artifacts/m7-backlog).",
    )
    parser.add_argument(
        "--issues-file",
        default=None,
        help="Optional JSON fixture file containing open GitHub issue objects.",
    )
    parser.add_argument(
        "--date",
        default=None,
        help="Override output date (YYYY-MM-DD). Defaults to UTC today.",
    )
    parser.add_argument(
        "--generated-at",
        default=None,
        help="Override generated_at timestamp (ISO-8601 UTC).",
    )
    return parser.parse_args()


def load_markers(markers_path: Path) -> list[dict[str, Any]]:
    if not markers_path.exists():
        emit_err(f"M7_BACKLOG:FAIL:missing_markers_json:{markers_path}")
        sys.exit(2)
    try:
        doc = json.loads(markers_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"M7_BACKLOG:FAIL:malformed_markers_json:{exc}")
        sys.exit(2)

    markers = doc.get("markers")
    if not isinstance(markers, list):
        emit_err("M7_BACKLOG:FAIL:invalid_markers_list")
        sys.exit(2)

    out: list[dict[str, Any]] = []
    for idx, row in enumerate(markers):
        if not isinstance(row, dict):
            emit_err(f"M7_BACKLOG:FAIL:invalid_marker_row:{idx}")
            sys.exit(2)
        out.append(row)
    return out


def _gh_api_open_issues(repo: str) -> list[dict[str, Any]]:
    if not shutil.which("gh"):
        emit_err("M7_BACKLOG:FAIL:gh_not_found")
        sys.exit(2)

    out: list[dict[str, Any]] = []
    page = 1
    while True:
        endpoint = f"repos/{repo}/issues?state=open&per_page=100&page={page}"
        proc = subprocess.run(
            ["gh", "api", endpoint],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        if proc.returncode != 0:
            emit_err(f"M7_BACKLOG:FAIL:gh_api_error:page={page}")
            if proc.stderr.strip():
                emit_err(proc.stderr.strip())
            sys.exit(2)
        try:
            rows = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            emit_err(f"M7_BACKLOG:FAIL:gh_api_json_parse_error:page={page}:{exc}")
            sys.exit(2)
        if not isinstance(rows, list):
            emit_err(f"M7_BACKLOG:FAIL:gh_api_non_list:page={page}")
            sys.exit(2)
        if not rows:
            break
        for row in rows:
            if isinstance(row, dict):
                out.append(row)
        if len(rows) < 100:
            break
        page += 1
    return out


def load_open_issues(repo: str, issues_file: Path | None) -> list[dict[str, Any]]:
    if issues_file:
        if not issues_file.exists():
            emit_err(f"M7_BACKLOG:FAIL:missing_issues_file:{issues_file}")
            sys.exit(2)
        try:
            data = json.loads(issues_file.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            emit_err(f"M7_BACKLOG:FAIL:malformed_issues_file:{exc}")
            sys.exit(2)
        if not isinstance(data, list):
            emit_err("M7_BACKLOG:FAIL:issues_file_non_list")
            sys.exit(2)
        rows = [row for row in data if isinstance(row, dict)]
    else:
        rows = _gh_api_open_issues(repo)

    # Filter out pull requests from /issues endpoint payload.
    return [row for row in rows if isinstance(row, dict) and "pull_request" not in row]


def issue_body_mentions_gate(body: str, gate: int) -> bool:
    patterns = (
        rf"\bgated\s+by\s+#\s*{gate}\b",
        rf"\bpre-#{gate}\b",
        rf"\bblocks\s+#\s*{gate}\b",
    )
    return any(re.search(p, body, flags=re.IGNORECASE) for p in patterns)


def build_summary(
    markers: list[dict[str, Any]],
    open_issues: list[dict[str, Any]],
    open_gating: list[int],
) -> dict[str, dict[str, Any]]:
    per_gate: dict[int, dict[str, Any]] = {
        gate: {
            "markers_pinned": 0,
            "issues_gated": 0,
            "harness_paths": [],
            "issue_numbers": [],
        }
        for gate in open_gating
    }

    harness_sets: dict[int, set[str]] = {gate: set() for gate in open_gating}
    issue_sets: dict[int, set[int]] = {gate: set() for gate in open_gating}

    for row in markers:
        gate = row.get("gatingIssue")
        name = row.get("name")
        reason = str(row.get("reason", ""))
        if not isinstance(gate, int) or gate not in per_gate:
            continue
        if not isinstance(name, str) or not name:
            continue
        # "SKIP-pinned" markers are represented by awaiting_* reasons.
        if not reason.startswith("awaiting_"):
            continue

        per_gate[gate]["markers_pinned"] += 1
        harness_sets[gate].add(f"tests/m7_toolchain/{name}.sh")

    for issue in open_issues:
        num = issue.get("number")
        body = issue.get("body")
        if not isinstance(num, int):
            continue
        text = body if isinstance(body, str) else ""
        for gate in open_gating:
            if issue_body_mentions_gate(text, gate):
                issue_sets[gate].add(num)

    for gate in open_gating:
        harness_paths = sorted(harness_sets[gate])
        issue_numbers = sorted(issue_sets[gate])
        per_gate[gate]["harness_paths"] = harness_paths
        per_gate[gate]["issue_numbers"] = issue_numbers
        per_gate[gate]["issues_gated"] = len(issue_numbers)

    # Convert int keys to #N format in fixed target order.
    return {f"#{gate}": per_gate[gate] for gate in open_gating}


def render_markdown(
    generated_at: str,
    repo: str,
    output_path: Path,
    per_gate: dict[str, dict[str, Any]],
) -> str:
    lines = [
        "# M7 harness backlog summary",
        "",
        f"- generated_at: `{generated_at}`",
        f"- repo: `{repo}`",
        f"- output_json: `{output_path.as_posix()}`",
        "",
        "| gating issue | markers pinned | open issues gated | harness paths |",
        "|---|---:|---:|---:|",
    ]

    for gate_key, row in per_gate.items():
        lines.append(
            f"| {gate_key} | {row['markers_pinned']} | {row['issues_gated']} | {len(row['harness_paths'])} |"
        )

    lines.extend(["", "## Issues referencing gates"])
    printed_any = False
    for gate_key, row in per_gate.items():
        nums = row["issue_numbers"]
        if not nums:
            continue
        printed_any = True
        labels = ", ".join(f"#{n}" for n in nums)
        lines.append(f"- {gate_key}: {labels}")
    if not printed_any:
        lines.append("- (none)")

    lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    markers_path = (
        Path(args.markers).resolve()
        if args.markers
        else (root / "tests" / "m7_toolchain" / "markers.json")
    )
    output_dir = (
        Path(args.output_dir).resolve()
        if args.output_dir
        else (root / "artifacts" / "m7-backlog")
    )
    issues_file = Path(args.issues_file).resolve() if args.issues_file else None

    now = dt.datetime.now(dt.timezone.utc)
    generated_at = (
        args.generated_at
        if args.generated_at
        else now.replace(microsecond=0).isoformat().replace("+00:00", "Z")
    )
    out_date = args.date if args.date else now.strftime("%Y-%m-%d")

    markers = load_markers(markers_path)
    open_issues = load_open_issues(args.repo, issues_file)
    open_issue_numbers = {
        row.get("number") for row in open_issues if isinstance(row.get("number"), int)
    }
    open_gating = [gate for gate in TARGET_GATING_ISSUES if gate in open_issue_numbers]

    per_gate = build_summary(markers, open_issues, open_gating)

    payload = {
        "generated_at": generated_at,
        "per_gating_issue": per_gate,
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    output_path = output_dir / f"summary-{out_date}.json"
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    print(render_markdown(generated_at, args.repo, output_path, per_gate), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
