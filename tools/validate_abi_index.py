#!/usr/bin/env python3
"""validate_abi_index.py — issue #630.

Drift gate for docs/abi/README.md:
- every docs/abi/*.md (except README.md itself) must be linked from README,
- README must not link to docs/abi/*.md files that do not exist.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path

LINK_RE = re.compile(r"\(([^)]+)\)")


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
    return p.parse_args()


def normalize_md_link(raw: str) -> str | None:
    # Drop anchor/query suffixes.
    link = raw.split("#", 1)[0].split("?", 1)[0].strip()
    if not link.endswith(".md"):
        return None
    if link.startswith("http://") or link.startswith("https://"):
        return None

    if link.startswith("./"):
        link = link[2:]
    if link.startswith("docs/abi/"):
        link = link[len("docs/abi/") :]

    # Only track sibling markdown files under docs/abi.
    if "/" in link:
        return None
    return link


def collect_readme_links(readme_path: Path) -> set[str]:
    text = readme_path.read_text(encoding="utf-8")
    found: set[str] = set()
    for m in LINK_RE.finditer(text):
        normalized = normalize_md_link(m.group(1))
        if normalized:
            found.add(normalized)
    return found


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    abi_dir = root / "docs" / "abi"
    readme = abi_dir / "README.md"

    if not abi_dir.is_dir():
        emit_err(f"ABI_INDEX:FAIL:missing_abi_dir:{abi_dir}")
        return 2
    if not readme.exists():
        emit_err(f"ABI_INDEX:FAIL:missing_readme:{readme}")
        return 2

    docs_on_disk = sorted(p.name for p in abi_dir.glob("*.md") if p.name != "README.md")
    linked = sorted(collect_readme_links(readme))

    docs_set = set(docs_on_disk)
    linked_set = set(linked)

    missing_links = sorted(docs_set - linked_set)
    dangling_links = sorted(linked_set - docs_set)

    if missing_links or dangling_links:
        for name in missing_links:
            emit_err(f"ABI_INDEX:FAIL:missing_link:{name}")
        for name in dangling_links:
            emit_err(f"ABI_INDEX:FAIL:dangling_link:{name}")

        expected_lines = [f"- {name}" for name in docs_on_disk]
        actual_lines = [f"- {name}" for name in linked]
        diff = difflib.unified_diff(
            expected_lines,
            actual_lines,
            fromfile="expected_docs_abi_md",
            tofile="readme_linked_docs_abi_md",
            lineterm="",
        )
        for line in diff:
            emit_err(f"ABI_INDEX:DIFF:{line}")

        emit_err(
            "ABI_INDEX:FAIL:summary:"
            f"missing={len(missing_links)}:dangling={len(dangling_links)}"
        )
        return 1

    for name in docs_on_disk:
        emit_out(f"ABI_INDEX:PASS:linked:{name}")
    emit_out(f"ABI_INDEX:PASS:summary:{len(docs_on_disk)}_docs_linked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
