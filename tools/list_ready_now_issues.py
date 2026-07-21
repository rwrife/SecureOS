#!/usr/bin/env python3
"""list_ready_now_issues.py — issue #626.

Read-only triage helper for merge-stall recovery.

The script enumerates open GitHub issues and surfaces "ready-now" candidates:
- title is not a daily-review snapshot,
- body does not reference open dependency issues in blocking patterns,
- issue is documentation or CI/stamp/drift-gate shaped.

By default this tool performs no GitHub write actions. Label application is
opt-in via --apply-label.
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

DEFAULT_GATING_ISSUES = [408, 409, 410, 421, 424, 522, 368, 585, 404, 406]

DAILY_REVIEW_RE = re.compile(r"^Daily review: \d{4}-\d{2}-\d{2}$")
# Dependency patterns called out by #626.
PATTERN_PRE = re.compile(r"\bpre-#\s*(\d+)\b", flags=re.IGNORECASE)
PATTERN_GATED_BY = re.compile(r"\bgated\s+by\s+#\s*(\d+)\b", flags=re.IGNORECASE)
PATTERN_DEPENDS = re.compile(r"\bdepends(?:\s+on|-on)\s+#\s*(\d+)\b", flags=re.IGNORECASE)
PATTERN_BLOCKED_BY = re.compile(r"\bblocked\s+by\s+#\s*(\d+)\b", flags=re.IGNORECASE)
PATTERN_BLOCKS = re.compile(r"\bblocks\s+#\s*(\d+)\b", flags=re.IGNORECASE)


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub owner/repo used for issue lookup via `gh api`.",
    )
    parser.add_argument(
        "--gating-issues",
        default=",".join(str(n) for n in DEFAULT_GATING_ISSUES),
        help="Comma-separated gating issue numbers to track.",
    )
    parser.add_argument(
        "--issues-file",
        default=None,
        help="Optional fixture JSON file with open issue objects (skips gh api).",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=25,
        help="Maximum candidate issues to emit.",
    )
    parser.add_argument(
        "--output-json",
        default=None,
        help="Optional path to write the JSON payload.",
    )
    parser.add_argument(
        "--generated-at",
        default=None,
        help="Override generated_at timestamp (ISO-8601 UTC) for deterministic fixtures.",
    )
    parser.add_argument(
        "--apply-label",
        action="store_true",
        help="Opt-in: apply --label-name to top --apply-limit candidates.",
    )
    parser.add_argument(
        "--label-name",
        default="ready-now",
        help="Label to apply when --apply-label is set.",
    )
    parser.add_argument(
        "--apply-limit",
        type=int,
        default=5,
        help="Number of top candidates to label when --apply-label is set.",
    )
    return parser.parse_args()


def parse_gating_issues(raw: str) -> list[int]:
    out: list[int] = []
    seen: set[int] = set()
    for part in raw.split(","):
        token = part.strip()
        if not token:
            continue
        if not token.isdigit():
            emit_err(f"READY_NOW:FAIL:invalid_gating_issue:{token}")
            sys.exit(2)
        num = int(token)
        if num in seen:
            continue
        seen.add(num)
        out.append(num)
    return out


def gh_api_open_issues(repo: str) -> list[dict[str, Any]]:
    if not shutil.which("gh"):
        emit_err("READY_NOW:FAIL:gh_not_found")
        sys.exit(2)

    rows: list[dict[str, Any]] = []
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
            emit_err(f"READY_NOW:FAIL:gh_api_error:page={page}")
            stderr = proc.stderr.strip()
            if stderr:
                emit_err(stderr)
            sys.exit(2)
        try:
            page_rows = json.loads(proc.stdout)
        except json.JSONDecodeError as exc:
            emit_err(f"READY_NOW:FAIL:gh_api_json_parse_error:page={page}:{exc}")
            sys.exit(2)
        if not isinstance(page_rows, list):
            emit_err(f"READY_NOW:FAIL:gh_api_non_list:page={page}")
            sys.exit(2)
        if not page_rows:
            break
        for row in page_rows:
            if isinstance(row, dict):
                rows.append(row)
        if len(page_rows) < 100:
            break
        page += 1
    return rows


def load_open_issues(repo: str, issues_file: Path | None) -> list[dict[str, Any]]:
    if issues_file:
        if not issues_file.exists():
            emit_err(f"READY_NOW:FAIL:missing_issues_file:{issues_file}")
            sys.exit(2)
        try:
            payload = json.loads(issues_file.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            emit_err(f"READY_NOW:FAIL:malformed_issues_file:{exc}")
            sys.exit(2)
        if not isinstance(payload, list):
            emit_err("READY_NOW:FAIL:issues_file_non_list")
            sys.exit(2)
        rows = [row for row in payload if isinstance(row, dict)]
    else:
        rows = gh_api_open_issues(repo)

    # /issues endpoint includes PRs; exclude them.
    return [row for row in rows if "pull_request" not in row]


def extract_dependency_refs(body: str) -> set[int]:
    refs: set[int] = set()
    for pattern in (
        PATTERN_PRE,
        PATTERN_GATED_BY,
        PATTERN_DEPENDS,
        PATTERN_BLOCKED_BY,
        PATTERN_BLOCKS,
    ):
        for m in pattern.finditer(body):
            refs.add(int(m.group(1)))
    return refs


def is_ci_or_stamp_slice(title: str, labels_lower: set[str]) -> bool:
    t = title.lower()
    if "ci" in labels_lower:
        return True
    if t.startswith("ci("):
        return True
    if "stamp" in t:
        return True
    if "drift-gate" in t or "drift gate" in t:
        return True
    if "validate_" in t:
        return True
    return False


def classify_ready_reason(title: str, labels_lower: set[str]) -> str | None:
    t = title.lower()
    is_doc = "documentation" in labels_lower or t.startswith("docs(")
    is_ci = is_ci_or_stamp_slice(title, labels_lower)
    if is_doc and is_ci:
        return "documentation + CI/drift-gate slice with no open dependency refs"
    if is_doc:
        return "documentation slice with no open dependency refs"
    if is_ci:
        return "CI/stamp/drift-gate slice with no open dependency refs"
    return None


def render_human_summary(payload: dict[str, Any]) -> str:
    lines: list[str] = []
    lines.append("# Ready-now issue candidates")
    lines.append("")
    lines.append(f"- generated_at: `{payload['generated_at']}`")
    lines.append(
        "- gating_issues_open: "
        + (", ".join(payload["gating_issues_open"]) if payload["gating_issues_open"] else "(none)")
    )
    lines.append(f"- candidate_count: {len(payload['candidates'])}")
    lines.append("")
    lines.append("| issue | labels | reason_ready |")
    lines.append("|---|---|---|")
    if payload["candidates"]:
        for row in payload["candidates"]:
            labels = ", ".join(row["labels"]) if row["labels"] else "(none)"
            lines.append(f"| #{row['number']} | {labels} | {row['reason_ready']} |")
    else:
        lines.append("| _(none)_ | - | - |")
    lines.append("")
    lines.append("## JSON")
    lines.append("")
    lines.append(json.dumps(payload, indent=2))
    lines.append("")
    return "\n".join(lines)


def ensure_label(repo: str, label: str) -> None:
    proc = subprocess.run(
        [
            "gh",
            "label",
            "create",
            label,
            "--repo",
            repo,
            "--color",
            "0E8A16",
            "--description",
            "No open dependency refs; candidate to merge during stall",
            "--force",
        ],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    if proc.returncode != 0:
        emit_err("READY_NOW:FAIL:label_create")
        stderr = proc.stderr.strip()
        if stderr:
            emit_err(stderr)
        sys.exit(2)


def apply_label(repo: str, label_name: str, numbers: list[int]) -> None:
    ensure_label(repo, label_name)
    for num in numbers:
        proc = subprocess.run(
            [
                "gh",
                "issue",
                "edit",
                str(num),
                "--repo",
                repo,
                "--add-label",
                label_name,
            ],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        if proc.returncode != 0:
            emit_err(f"READY_NOW:FAIL:label_apply:#{num}")
            stderr = proc.stderr.strip()
            if stderr:
                emit_err(stderr)
            sys.exit(2)
        emit_out(f"READY_NOW:LABEL_APPLIED:#{num}:{label_name}")


def main() -> int:
    args = parse_args()
    if args.limit <= 0:
        emit_err("READY_NOW:FAIL:limit_must_be_positive")
        return 2
    if args.apply_limit <= 0:
        emit_err("READY_NOW:FAIL:apply_limit_must_be_positive")
        return 2

    issues_file = Path(args.issues_file).resolve() if args.issues_file else None
    output_json = Path(args.output_json).resolve() if args.output_json else None
    gating_issues = parse_gating_issues(args.gating_issues)

    rows = load_open_issues(args.repo, issues_file)
    open_issue_numbers = {row.get("number") for row in rows if isinstance(row.get("number"), int)}
    open_issue_numbers_int = {n for n in open_issue_numbers if isinstance(n, int)}

    gating_open_int = [n for n in gating_issues if n in open_issue_numbers_int]
    gating_open = [f"#{n}" for n in gating_open_int]

    candidates: list[dict[str, Any]] = []
    for row in rows:
        num = row.get("number")
        title = row.get("title")
        if not isinstance(num, int) or not isinstance(title, str):
            continue
        if DAILY_REVIEW_RE.match(title.strip()):
            continue

        raw_body = row.get("body")
        body: str = raw_body if isinstance(raw_body, str) else ""
        dep_refs = extract_dependency_refs(body)
        open_dep_refs = sorted(n for n in dep_refs if n in open_issue_numbers_int)

        # Exclude if any open dependency-like references are present.
        if open_dep_refs:
            continue

        raw_labels = row.get("labels")
        labels_data: list[Any] = raw_labels if isinstance(raw_labels, list) else []
        labels: list[str] = []
        for raw in labels_data:
            if isinstance(raw, dict) and isinstance(raw.get("name"), str):
                labels.append(raw["name"])
        labels_sorted = sorted(labels, key=str.lower)
        labels_lower = {x.lower() for x in labels_sorted}

        reason = classify_ready_reason(title, labels_lower)
        if not reason:
            continue

        candidates.append(
            {
                "number": num,
                "title": title,
                "labels": labels_sorted,
                "reason_ready": reason,
            }
        )

    # Rank docs first, then CI/stamp, then by issue number ascending.
    def rank_key(item: dict[str, Any]) -> tuple[int, int]:
        labels_lower = {x.lower() for x in item["labels"]}
        title = str(item["title"])
        is_doc = "documentation" in labels_lower or title.lower().startswith("docs(")
        is_ci = is_ci_or_stamp_slice(title, labels_lower)
        if is_doc and is_ci:
            prio = 0
        elif is_doc:
            prio = 1
        elif is_ci:
            prio = 2
        else:
            prio = 3
        return (prio, int(item["number"]))

    candidates.sort(key=rank_key)
    candidates = candidates[: args.limit]

    generated_at = (
        args.generated_at
        if args.generated_at
        else dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    )

    payload = {
        "generated_at": generated_at,
        "gating_issues_open": gating_open,
        "candidates": candidates,
    }

    if output_json:
        output_json.parent.mkdir(parents=True, exist_ok=True)
        output_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        emit_out(f"READY_NOW:PASS:json:{output_json}")

    emit_out(render_human_summary(payload))

    if args.apply_label:
        if not shutil.which("gh"):
            emit_err("READY_NOW:FAIL:gh_not_found_for_labeling")
            return 2
        top_numbers = [row["number"] for row in candidates[: args.apply_limit]]
        apply_label(args.repo, args.label_name, top_numbers)

    emit_out(f"READY_NOW:PASS:candidates:{len(candidates)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
