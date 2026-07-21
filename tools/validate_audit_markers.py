#!/usr/bin/env python3
"""Validate docs/abi audit-marker markdown <-> json drift.

Issue #591
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

MARKER = "AUDIT_MARKERS"


def out(msg: str) -> None:
    print(msg, flush=True)


def err(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        err(f"{MARKER}:FAIL:missing_file:{path}")
        raise SystemExit(2)


def parse_markdown_prefixes(path: Path) -> set[str]:
    text = read_text(path)
    lines = text.splitlines()

    in_catalog = False
    prefixes: set[str] = set()

    for line in lines:
        if line.startswith("## 3. Marker catalog"):
            in_catalog = True
            continue
        if in_catalog and line.startswith("## "):
            break
        if not in_catalog:
            continue
        if not line.strip().startswith("|"):
            continue

        cols = [c.strip() for c in line.strip().split("|")[1:-1]]
        if len(cols) < 1:
            continue
        first = cols[0]
        if first in {"marker_prefix", "---"}:
            continue

        # Ignore explanatory parenthetical tails in the prefix cell, e.g.
        # `launch.granted` / `launch.denied` (`owner_kind=...`).
        first_core = first.split("(", 1)[0].strip()

        tokens = re.findall(r"`([^`]+)`", first_core)
        if not tokens:
            err(f"{MARKER}:FAIL:markdown_marker_prefix_cell_missing_backticks:{first}")
            raise SystemExit(2)

        for tok in tokens:
            norm = tok.strip()
            if not norm:
                continue
            prefixes.add(norm)

    if not prefixes:
        err(f"{MARKER}:FAIL:no_prefixes_parsed_from_markdown:{path}")
        raise SystemExit(2)

    return prefixes


def parse_json_markers(path: Path) -> list[dict[str, Any]]:
    try:
        doc = json.loads(read_text(path))
    except json.JSONDecodeError as exc:
        err(f"{MARKER}:FAIL:malformed_json:{exc}")
        raise SystemExit(2)

    if doc.get("schemaVersion") != 1:
        err(f"{MARKER}:FAIL:unsupported_schema:{doc.get('schemaVersion')!r}")
        raise SystemExit(2)

    rows = doc.get("markers")
    if not isinstance(rows, list):
        err(f"{MARKER}:FAIL:invalid_markers_array")
        raise SystemExit(2)

    out_rows: list[dict[str, Any]] = []
    seen: set[str] = set()

    for idx, row in enumerate(rows):
        if not isinstance(row, dict):
            err(f"{MARKER}:FAIL:invalid_marker_row:{idx}")
            raise SystemExit(2)
        prefix = row.get("prefix")
        if not isinstance(prefix, str) or not prefix.strip():
            err(f"{MARKER}:FAIL:invalid_prefix:{idx}:{prefix!r}")
            raise SystemExit(2)
        prefix = prefix.strip()
        if prefix in seen:
            err(f"{MARKER}:FAIL:duplicate_prefix:{prefix}")
            raise SystemExit(2)
        seen.add(prefix)
        out_rows.append(dict(row, prefix=prefix))

    if not out_rows:
        err(f"{MARKER}:FAIL:no_markers_in_json")
        raise SystemExit(2)

    return out_rows


def check_issue_state(issue: int) -> str | None:
    cmd = ["gh", "issue", "view", str(issue), "--json", "state", "--jq", ".state"]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None
    state = proc.stdout.strip()
    return state or None


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate audit marker markdown/json drift")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument(
        "--markdown",
        default="docs/abi/audit-markers.md",
        help="markdown registry path (relative to --root or absolute)",
    )
    ap.add_argument(
        "--json",
        default="docs/abi/audit-markers.json",
        help="json registry path (relative to --root or absolute)",
    )
    ap.add_argument(
        "--with-gh",
        action="store_true",
        help="check gating_issue states via gh (closed issues fail; offline is SKIP)",
    )
    args = ap.parse_args()

    root = Path(args.root).resolve()
    md_path = Path(args.markdown)
    js_path = Path(args.json)
    if not md_path.is_absolute():
        md_path = root / md_path
    if not js_path.is_absolute():
        js_path = root / js_path

    md_prefixes = parse_markdown_prefixes(md_path)
    json_rows = parse_json_markers(js_path)
    json_prefixes = {row["prefix"] for row in json_rows}

    failures = 0

    missing_in_json = sorted(md_prefixes - json_prefixes)
    missing_in_md = sorted(json_prefixes - md_prefixes)

    for pref in missing_in_json:
        err(f"{MARKER}:FAIL:missing_in_json:{pref}")
        failures += 1
    for pref in missing_in_md:
        err(f"{MARKER}:FAIL:missing_in_markdown:{pref}")
        failures += 1

    if args.with_gh:
        seen_issues: set[int] = set()
        gh_lookup_failed = False
        for row in json_rows:
            gating = row.get("gating_issue")
            prefix = row["prefix"]
            if gating is None:
                continue
            if not isinstance(gating, int) or gating <= 0:
                err(f"{MARKER}:FAIL:invalid_gating_issue:{prefix}:{gating!r}")
                failures += 1
                continue
            if gating in seen_issues:
                continue
            seen_issues.add(gating)
            state = check_issue_state(gating)
            if state is None:
                out(f"{MARKER}:SKIP:gating_issue_state_unknown:{gating}:gh_unavailable_or_offline")
                gh_lookup_failed = True
                continue
            if state.upper() == "CLOSED":
                err(f"{MARKER}:FAIL:gating_issue_closed:{gating}")
                failures += 1
            elif state.upper() == "OPEN":
                out(f"{MARKER}:PASS:gating_issue_open:{gating}")
            else:
                err(f"{MARKER}:FAIL:unexpected_issue_state:{gating}:{state}")
                failures += 1

        if gh_lookup_failed:
            out(f"{MARKER}:SKIP:gating_issue_check_partial")
    else:
        out(f"{MARKER}:SKIP:gating_issue_check_disabled:use_--with-gh")

    if failures:
        err(f"{MARKER}:FAIL:summary:{failures}_failures")
        return 1

    out(
        f"{MARKER}:PASS:summary:markdown_prefixes={len(md_prefixes)}:json_prefixes={len(json_prefixes)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
