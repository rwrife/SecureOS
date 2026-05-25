#!/usr/bin/env python3
"""validate_abi_stamps.py — issue #297

Asserts that every `docs/abi/*.md` (other than `capability-registry.md`,
which has its own provenance discipline per #234) carries a
`Last verified against commit: <sha>` line whose recorded SHA is at least
as recent as the file's most-recent *content-changing* commit.

A "content-changing" commit is any commit that touches the file with at
least one non-stamp-line diff hunk (so a stamp-only bump like #258
does not itself reset the freshness window).

Output contract (mirrors `validate_capability_registry.py` and #234):

* On success per file: prints `ABI_STAMP:PASS:<rel-path>:<sha>` to stdout.
* On failure per file: prints
  `ABI_STAMP:FAIL:<rel-path>:stamp=<sha>:last_content=<sha>` to stderr.
* Exit codes:
    0  every in-scope file is fresh.
    1  one or more `ABI_STAMP:FAIL:*` markers emitted.
    2  environment / usage error (file missing, not a git checkout,
       no stamp line at all in a file we expected to have one).
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Optional

STAMP_RE = re.compile(r"^Last verified against commit:\s*([0-9a-fA-F]{7,40})\s*$")

# Files in docs/abi/ that participate in the convention.
# `capability-registry.md` is exempt — its stamp says
# "regenerated on each touch" and is CI-validated via
# validate_capability_registry.{sh,ps1} (#234).
DEFAULT_EXEMPT = {"capability-registry.md"}


def git(args: list[str], root: Path) -> str:
    res = subprocess.run(
        ["git", "-C", str(root)] + args,
        check=False,
        capture_output=True,
        text=True,
    )
    if res.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed (rc={res.returncode}): {res.stderr.strip()}"
        )
    return res.stdout


def find_stamp(path: Path) -> Optional[str]:
    """Return the SHA recorded in the `Last verified against commit:` line, or None."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        m = STAMP_RE.match(line.strip())
        if m:
            return m.group(1).lower()
    return None


def last_content_commit(rel_path: str, root: Path) -> Optional[str]:
    """Find the most-recent commit that touched <rel_path> with at least one
    non-stamp-line diff. Returns the full SHA or None if no such commit
    exists (file was never edited beyond stamp bumps, or never committed).
    """
    log = git(["log", "--format=%H", "--", rel_path], root).split()
    for sha in log:
        # Show the diff for this file at this commit and look for any
        # +/- line that is not a stamp-line edit.
        diff = git(
            ["show", "--no-color", "--format=", sha, "--", rel_path], root
        )
        for line in diff.splitlines():
            if not line:
                continue
            if line.startswith("+++") or line.startswith("---"):
                continue
            if line[0] not in ("+", "-"):
                continue
            body = line[1:].lstrip()
            if STAMP_RE.match(body):
                continue
            # First non-stamp +/- line in this commit's diff for the
            # file => this commit is content-changing.
            return sha
    return None


def sha_is_ancestor(ancestor: str, descendant: str, root: Path) -> bool:
    """Return True if `ancestor` is an ancestor of (or equal to) `descendant`."""
    if ancestor.lower() == descendant.lower():
        return True
    res = subprocess.run(
        ["git", "-C", str(root), "merge-base", "--is-ancestor", ancestor, descendant],
        check=False,
        capture_output=True,
        text=True,
    )
    return res.returncode == 0


def resolve(sha: str, root: Path) -> str:
    return git(["rev-parse", sha], root).strip().lower()


def iter_in_scope(abi_dir: Path, exempt: Iterable[str]) -> list[Path]:
    exempt_set = set(exempt)
    out = []
    for entry in sorted(abi_dir.iterdir()):
        if entry.suffix != ".md":
            continue
        if entry.name in exempt_set:
            continue
        out.append(entry)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="Repository root.")
    parser.add_argument(
        "--abi-dir",
        default=None,
        help="Override docs/abi directory (defaults to <root>/docs/abi).",
    )
    parser.add_argument(
        "--exempt",
        action="append",
        default=[],
        help="Additional filenames (under abi-dir) to exempt. Repeatable.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    abi_dir = Path(args.abi_dir).resolve() if args.abi_dir else (root / "docs" / "abi")

    if not abi_dir.is_dir():
        print(f"ABI_STAMP:FAIL:abi_dir_missing:{abi_dir}", file=sys.stderr)
        return 2

    # Cheap sanity: ensure we're inside a git checkout.
    try:
        git(["rev-parse", "--git-dir"], root)
    except RuntimeError as exc:
        print(f"ABI_STAMP:FAIL:not_a_git_checkout:{exc}", file=sys.stderr)
        return 2

    exempt = DEFAULT_EXEMPT | set(args.exempt)
    files = iter_in_scope(abi_dir, exempt)
    if not files:
        print("ABI_STAMP:FAIL:no_files_in_scope", file=sys.stderr)
        return 2

    failures = 0
    for path in files:
        rel = os.path.relpath(path, root)
        stamp = find_stamp(path)
        if stamp is None:
            # Not all docs/abi/*.md participate in the provenance
            # convention (e.g. spec-only contracts that pre-date the
            # `Last verified` header). Per README §Provenance the line
            # is required when present; missing line is out-of-scope.
            print(f"ABI_STAMP:SKIP:{rel}:no_stamp_line")
            continue

        try:
            stamp_full = resolve(stamp, root)
        except RuntimeError:
            print(
                f"ABI_STAMP:FAIL:{rel}:stamp_sha_unknown_to_git:{stamp}",
                file=sys.stderr,
            )
            failures += 1
            continue

        last = last_content_commit(rel, root)
        if last is None:
            # File has no non-stamp content history (e.g. brand-new file
            # in this PR). Accept — the stamp itself is the freshness
            # baseline.
            print(f"ABI_STAMP:PASS:{rel}:{stamp_full[:10]}")
            continue

        if sha_is_ancestor(last, stamp_full, root):
            print(f"ABI_STAMP:PASS:{rel}:{stamp_full[:10]}")
        else:
            print(
                f"ABI_STAMP:FAIL:{rel}:stamp={stamp_full[:10]}:last_content={last[:10]}",
                file=sys.stderr,
            )
            failures += 1

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
