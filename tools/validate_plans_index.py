#!/usr/bin/env python3
"""validate_plans_index.py — issue #582.

Drift gate for plans/README.md index coverage.

Contract:
- every plans/*.md file (except README.md) must appear exactly once in the
  README index list,
- no index entry may point to a non-existent plans file.

Exit codes:
  0 — all plans are indexed exactly once
  1 — one or more plans are missing / duplicated / stale-indexed
  2 — usage / environment error
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path


BULLET_PLAN_RE = re.compile(r"^\s*-\s+`([^`]+\.md)`\s*$", re.MULTILINE)


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    p.add_argument(
        "--plans-dir",
        default="plans",
        help="Relative path to plans directory.",
    )
    p.add_argument(
        "--index",
        default="plans/README.md",
        help="Relative path to plans index markdown.",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    plans_dir = (root / args.plans_dir).resolve()
    index_path = (root / args.index).resolve()

    if not plans_dir.is_dir():
        emit_err(f"PLANS_INDEX:ERROR:missing_plans_dir:{plans_dir}")
        return 2
    if not index_path.is_file():
        emit_err(f"PLANS_INDEX:ERROR:missing_index_file:{index_path}")
        return 2

    plan_files = sorted(p.name for p in plans_dir.glob("*.md") if p.name != "README.md")
    plan_set = set(plan_files)

    index_text = index_path.read_text(encoding="utf-8")
    indexed_names = [m.group(1) for m in BULLET_PLAN_RE.finditer(index_text)]
    indexed_counts = Counter(indexed_names)

    failures = 0

    # Missing / duplicate coverage for concrete plan files.
    for name in plan_files:
        count = indexed_counts.get(name, 0)
        rel = f"plans/{name}"
        if count == 0:
            emit_err(f"PLANS_INDEX:FAIL:{rel}:not_indexed")
            failures += 1
        elif count > 1:
            emit_err(f"PLANS_INDEX:FAIL:{rel}:duplicate_index_entries:{count}")
            failures += 1
        else:
            emit_out(f"PLANS_INDEX:PASS:{rel}:indexed_once")

    # Stale index entries (present in README list but file not on disk).
    for name, count in sorted(indexed_counts.items()):
        if name == "README.md":
            continue
        if name not in plan_set:
            emit_err(f"PLANS_INDEX:FAIL:plans/{name}:indexed_but_missing")
            failures += 1
        elif count > 1:
            # Already reported above for real files; keep deterministic output
            # to make the duplicate visible in this loop too.
            emit_err(f"PLANS_INDEX:FAIL:plans/{name}:duplicate_index_entries:{count}")

    if failures:
        emit_err(f"PLANS_INDEX:FAIL:summary:{failures}_failures")
        return 1

    emit_out(f"PLANS_INDEX:PASS:summary:{len(plan_files)}_plans_indexed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
