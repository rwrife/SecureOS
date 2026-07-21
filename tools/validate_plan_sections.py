#!/usr/bin/env python3
"""validate_plan_sections.py — issue #593.

Drift gate for plan slice-token ↔ GitHub issue linkage.

Contract:
- scan plans/*.md for canonical milestone slice tokens (e.g. M7-TOOLCHAIN-005),
- classify each token as open / closed-merged / unticketed via `gh issue list`,
- FAIL when a token is unticketed and not listed in plans/.unticketed-allowlist.

Networked lookups are opt-in (`--with-gh`). Without it, the validator emits
SKIP markers and exits 0 so offline/dev runs stay deterministic.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Examples matched:
#   M1-CAPTBL-006
#   M5-SUBSTRATE
#   M5-SUBSTRATE-005a
TOKEN_RE = re.compile(r"\b(M[0-9]+-[A-Z0-9]+(?:-[A-Z0-9]+)*[a-z]?)\b")


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
        "--allowlist",
        default="plans/.unticketed-allowlist",
        help="Relative path to unticketed slice allowlist.",
    )
    p.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub owner/repo used for gh issue lookups.",
    )
    p.add_argument(
        "--with-gh",
        action="store_true",
        help="Enable gh-backed issue lookups. Without this flag, emit SKIP and exit 0.",
    )
    p.add_argument(
        "--gh-limit",
        type=int,
        default=20,
        help="Max issue search results per token.",
    )
    return p.parse_args()


def load_allowlist(path: Path) -> dict[str, str]:
    if not path.exists():
        emit_err(f"PLAN_SECTION:ERROR:missing_allowlist:{path}")
        raise SystemExit(2)

    out: dict[str, str] = {}
    for idx, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "\t" in line:
            token, rationale = line.split("\t", 1)
        else:
            parts = line.split(maxsplit=1)
            token = parts[0]
            rationale = parts[1] if len(parts) > 1 else ""
        token = token.strip()
        rationale = rationale.strip()

        if not TOKEN_RE.fullmatch(token):
            emit_err(
                f"PLAN_SECTION:ERROR:allowlist_invalid_token:{path}:{idx}:{token}"
            )
            raise SystemExit(2)
        out[token] = rationale
    return out


def extract_tokens(plan_path: Path) -> list[str]:
    tokens: set[str] = set()
    in_code_fence = False
    for raw in plan_path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip("\n")
        if line.strip().startswith("```"):
            in_code_fence = not in_code_fence
            continue
        if in_code_fence:
            continue
        for token in TOKEN_RE.findall(line):
            tokens.add(token)
    return sorted(tokens)


def gh_issue_list_for_token(token: str, repo: str, limit: int) -> list[dict]:
    proc = subprocess.run(
        [
            "gh",
            "issue",
            "list",
            "--repo",
            repo,
            "--state",
            "all",
            "--search",
            token,
            "--limit",
            str(limit),
            "--json",
            "number,state,title",
        ],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError("gh_issue_list_failed")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"gh_json_parse:{exc}") from exc
    if not isinstance(data, list):
        raise RuntimeError("gh_json_not_list")
    cleaned: list[dict] = []
    for row in data:
        if not isinstance(row, dict):
            continue
        n = row.get("number")
        s = str(row.get("state", "")).upper()
        if isinstance(n, int) and s in {"OPEN", "CLOSED"}:
            cleaned.append({"number": n, "state": s})
    cleaned.sort(key=lambda r: r["number"])
    return cleaned


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    plans_dir = (root / args.plans_dir).resolve()
    allowlist_path = (root / args.allowlist).resolve()

    if not plans_dir.is_dir():
        emit_err(f"PLAN_SECTION:ERROR:missing_plans_dir:{plans_dir}")
        return 2

    allowlist = load_allowlist(allowlist_path)

    plan_files = sorted(p for p in plans_dir.glob("*.md") if p.name != "README.md")
    plan_token_map: dict[Path, list[str]] = {
        plan: extract_tokens(plan) for plan in plan_files
    }

    total_tokens = sum(len(v) for v in plan_token_map.values())

    if not args.with_gh:
        emit_out("PLAN_SECTION:SKIP:gh_lookup_disabled:use_--with-gh")
        emit_out(
            f"PLAN_SECTION:PASS:summary:plans={len(plan_files)}:tokens={total_tokens}:mode=offline"
        )
        return 0

    if not shutil.which("gh"):
        emit_out("PLAN_SECTION:SKIP:gh_not_found")
        emit_out(
            f"PLAN_SECTION:PASS:summary:plans={len(plan_files)}:tokens={total_tokens}:mode=offline"
        )
        return 0

    cache: dict[str, list[dict] | None] = {}
    failures = 0

    for plan in plan_files:
        rel = str(plan.relative_to(root))
        tokens = plan_token_map[plan]
        if not tokens:
            emit_out(f"PLAN_SECTION:SKIP:{rel}:no_slice_tokens")
            continue

        for token in tokens:
            if token not in cache:
                try:
                    cache[token] = gh_issue_list_for_token(token, args.repo, args.gh_limit)
                except Exception:
                    cache[token] = None

            rows = cache[token]
            if rows is None:
                emit_out(f"PLAN_SECTION:SKIP:{rel}:{token}:gh_query_failed")
                continue

            open_rows = [r for r in rows if r["state"] == "OPEN"]
            closed_rows = [r for r in rows if r["state"] == "CLOSED"]

            if open_rows:
                emit_out(
                    f"PLAN_SECTION:PASS:{rel}:{token}:open:#{open_rows[0]['number']}"
                )
            elif closed_rows:
                emit_out(
                    f"PLAN_SECTION:PASS:{rel}:{token}:closed-merged:#{closed_rows[-1]['number']}"
                )
            else:
                if token in allowlist:
                    rationale = allowlist[token] or "allowlisted"
                    emit_out(
                        f"PLAN_SECTION:PASS:{rel}:{token}:unticketed_allowlisted:{rationale}"
                    )
                else:
                    emit_err(f"PLAN_SECTION:FAIL:{rel}:{token}:UNTICKETED")
                    failures += 1

    if failures:
        emit_err(f"PLAN_SECTION:FAIL:summary:{failures}_unticketed_tokens")
        return 1

    emit_out(
        f"PLAN_SECTION:PASS:summary:plans={len(plan_files)}:tokens={total_tokens}:with_gh=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
