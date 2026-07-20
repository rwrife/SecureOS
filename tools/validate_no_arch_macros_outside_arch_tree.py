#!/usr/bin/env python3
"""Issue #623 portability drift validator.

Fails when architecture preprocessor macros leak outside kernel/arch/**.
The scan target is the kernel non-arch tree listed in the issue body.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

MACRO_RE = re.compile(r"\b(__x86_64__|__i386__|__amd64__|__aarch64__|__arm__)\b")
TARGET_DIRS = (
    "kernel/core",
    "kernel/mem",
    "kernel/sched",
    "kernel/ipc",
    "kernel/cap",
    "kernel/proc",
    "kernel/hal",
    "kernel/fs",
    "kernel/drivers",
    "kernel/format",
    "kernel/svc",
    "kernel/event",
    "kernel/gfx",
    "kernel/clock",
    "kernel/crypto",
    "kernel/lib",
    "kernel/user",
)
FILE_SUFFIXES = {".c", ".h", ".S"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--allowlist", type=Path, required=True)
    return parser.parse_args()


def load_allowlist(path: Path) -> set[str]:
    if not path.exists():
        return set()
    out: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        out.add(line)
    return out


def iter_target_files(root: Path):
    for rel_dir in TARGET_DIRS:
        abs_dir = root / rel_dir
        if not abs_dir.is_dir():
            continue
        for path in sorted(abs_dir.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix not in FILE_SUFFIXES:
                continue
            if path.name.endswith("_asm.S"):
                # Explicitly exempt assembler entry files.
                continue
            yield path


def strip_c_comments(line: str, in_block: bool) -> tuple[str, bool]:
    i = 0
    out_chars: list[str] = []

    while i < len(line):
        if in_block:
            end = line.find("*/", i)
            if end == -1:
                return "".join(out_chars), True
            i = end + 2
            in_block = False
            continue

        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue

        out_chars.append(line[i])
        i += 1

    return "".join(out_chars), in_block


def validate(root: Path, allowlist: set[str]) -> list[tuple[str, int, str, str]]:
    failures: list[tuple[str, int, str, str]] = []

    for path in iter_target_files(root):
        rel = path.relative_to(root).as_posix()
        if rel in allowlist:
            continue

        in_block = False
        for line_no, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
            stripped, in_block = strip_c_comments(raw, in_block)
            if not stripped.strip():
                continue
            match = MACRO_RE.search(stripped)
            if not match:
                continue
            snippet = " ".join(stripped.strip().split())
            failures.append((rel, line_no, match.group(1), snippet))

    return failures


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    allowlist = load_allowlist(args.allowlist)

    failures = validate(root, allowlist)
    if failures:
        print(f"ARCH_MACRO_VALIDATE:FAIL:count={len(failures)}")
        for rel, line_no, macro, snippet in failures:
            print(f"ARCH_MACRO_VALIDATE:FAIL:{rel}:{line_no}:{macro}:{snippet}")
        return 1

    print("ARCH_MACRO_VALIDATE:PASS:no_arch_macros_outside_arch_tree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
