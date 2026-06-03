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

Strict-no-skip mode (#470):
  Pass ``--strict-no-skip`` (or set ``STRICT_STAMPS=1`` in the env, which
  the build/scripts wrapper translates into the flag) to promote the
  ``ABI_STAMP:SKIP:<file>:no_stamp_line`` arm to
  ``ABI_STAMP:FAIL:<file>:no_stamp_line`` (exit 1). Use ``--exempt <name>``
  (repeatable) to drop a filename under ``docs/abi/`` from iteration
  entirely so it is neither PASS-checked nor FAIL-ed under strict mode
  (e.g. genuinely non-freshness index pages); this matches the existing
  semantics for ``capability-registry.md``.

Strict-no-placeholder mode (#470 co-scope, called out in PR #479):
  Pass ``--strict-no-placeholder`` (or set ``STRICT_PLACEHOLDER=1`` in the
  env, which the build/scripts wrapper translates into the flag) to
  promote a ``Last verified against commit:`` line whose value is not a
  7-40 char hex SHA (e.g. ``HEAD``, ``TBD``, ``unknown``) from
  ``ABI_STAMP:SKIP:<file>:placeholder:<token>`` to
  ``ABI_STAMP:FAIL:<file>:placeholder:<token>`` (exit 1). This is the
  forcing-function for the gap originally tracked by #463
  (``docs/abi/manifest.md`` shipped with a literal ``HEAD`` placeholder
  that the strict-SHA regex silently dropped to ``no_stamp_line`` SKIP).
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
# Looser detection: any ``Last verified against commit:`` line, regardless
# of whether the value parses as a hex SHA. Used to distinguish a genuine
# missing-line (``no_stamp_line``) from a placeholder-token line
# (``placeholder:<token>``) under --strict-no-placeholder (#470).
STAMP_LINE_RE = re.compile(r"^Last verified against commit:\s*(\S.*?)\s*$")

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
    """Return the SHA recorded in the `Last verified against commit:` line, or None.

    Kept as a backward-compatible helper. New callers should prefer
    :func:`find_stamp_line` so the placeholder arm (#470) can be
    distinguished from a genuinely-missing line.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        m = STAMP_RE.match(line.strip())
        if m:
            return m.group(1).lower()
    return None


def find_stamp_line(path: Path) -> tuple[Optional[str], Optional[str]]:
    """Classify the file's ``Last verified against commit:`` line.

    Returns ``("sha", <sha lowercased>)`` if the line is present and the
    value is a 7-40 char hex SHA; ``("placeholder", <token>)`` if the
    line is present but the value is not a hex SHA (e.g. ``HEAD``,
    ``TBD``); ``(None, None)`` if no such line exists. Returning the
    placeholder token verbatim (subject to a small length cap to keep
    the marker single-line) lets the FAIL/SKIP marker self-describe the
    offending value for the failing-PR feedback loop.
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return (None, None)
    # Prefer the strict-SHA match — first SHA wins, matching the
    # historical :func:`find_stamp` semantics. Only fall through to the
    # looser STAMP_LINE_RE if no SHA-shaped line is found, so a file
    # that happens to contain both a placeholder line *and* a real SHA
    # stamp keeps the existing PASS/FAIL path.
    placeholder: Optional[str] = None
    for line in text.splitlines():
        stripped = line.strip()
        m = STAMP_RE.match(stripped)
        if m:
            return ("sha", m.group(1).lower())
        if placeholder is None:
            m2 = STAMP_LINE_RE.match(stripped)
            if m2:
                token = m2.group(1)
                # Cap the echoed token so a pathologically-long value
                # cannot corrupt the marker grammar consumed by
                # validate_bundle.sh.
                if len(token) > 64:
                    token = token[:64] + "..."
                placeholder = token
    if placeholder is not None:
        return ("placeholder", placeholder)
    return (None, None)


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
    parser.add_argument(
        "--strict-no-skip",
        action="store_true",
        default=False,
        help=(
            "Issue #470: promote 'no_stamp_line' SKIP to FAIL (exit 1) for "
            "any in-scope docs/abi/*.md missing the 'Last verified against "
            "commit:' line. Files listed via --exempt remain SKIP-eligible."
        ),
    )
    parser.add_argument(
        "--strict-no-placeholder",
        action="store_true",
        default=False,
        help=(
            "Issue #470 (sibling of --strict-no-skip): promote a "
            "'Last verified against commit:' line whose value is not a "
            "7-40 char hex SHA from "
            "'ABI_STAMP:SKIP:<file>:placeholder:<token>' to "
            "'ABI_STAMP:FAIL:<file>:placeholder:<token>' (exit 1). "
            "Forcing-function for the #463 'HEAD' placeholder shape that "
            "the strict-SHA regex used to silently drop to no_stamp_line."
        ),
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

    # Refuse to run against a shallow clone: `git log -- <path>` on a
    # shallow checkout returns only the visible tip, which would make
    # every file appear to have been (re)introduced in that one commit
    # and silently inflate freshness failures. CI is responsible for
    # `fetch-depth: 0` (see .github/workflows/pr-build.yml); fail loudly
    # if that ever regresses rather than emitting confusing FAIL markers.
    try:
        shallow = git(["rev-parse", "--is-shallow-repository"], root).strip()
    except RuntimeError:
        shallow = "false"
    if shallow == "true":
        print(
            "ABI_STAMP:FAIL:shallow_clone:"
            "need_full_history_for_per_file_log",
            file=sys.stderr,
        )
        return 2

    exempt = DEFAULT_EXEMPT | set(args.exempt)
    files = iter_in_scope(abi_dir, exempt)
    if not files:
        print("ABI_STAMP:FAIL:no_files_in_scope", file=sys.stderr)
        return 2

    failures = 0
    for path in files:
        rel = os.path.relpath(path, root)
        kind, value = find_stamp_line(path)
        if kind is None:
            # Not all docs/abi/*.md participate in the provenance
            # convention (e.g. spec-only contracts that pre-date the
            # `Last verified` header). Per README §Provenance the line
            # is required when present; missing line is out-of-scope
            # unless --strict-no-skip is in force (#470). Files genuinely
            # outside the freshness contract are dropped from `files` up
            # in iter_in_scope() via --exempt, so they never reach this
            # branch.
            if args.strict_no_skip:
                print(
                    f"ABI_STAMP:FAIL:{rel}:no_stamp_line",
                    file=sys.stderr,
                )
                failures += 1
            else:
                print(f"ABI_STAMP:SKIP:{rel}:no_stamp_line")
            continue
        if kind == "placeholder":
            # The file has a `Last verified against commit:` line but its
            # value is not a hex SHA (e.g. `HEAD`, `TBD`). Historically
            # the strict-SHA regex silently dropped this case to
            # `no_stamp_line` SKIP (see #463 / #470 motivation). Under
            # --strict-no-placeholder promote it to FAIL so a future
            # placeholder cannot bypass the gate; otherwise emit a
            # distinct SKIP reason so the bundle report can surface the
            # offending token in the JSON without changing exit status.
            if args.strict_no_placeholder:
                print(
                    f"ABI_STAMP:FAIL:{rel}:placeholder:{value}",
                    file=sys.stderr,
                )
                failures += 1
            else:
                print(f"ABI_STAMP:SKIP:{rel}:placeholder:{value}")
            continue
        # kind == "sha"
        stamp = value or ""

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
