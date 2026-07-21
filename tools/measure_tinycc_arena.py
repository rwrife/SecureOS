#!/usr/bin/env python3
"""measure_tinycc_arena.py — issue #543.

Phase-4 measurement harness for TinyCC arena sizing.

Current repository state (while #408 Phase 3 is still OPEN) does not yet ship
an instrumentable freestanding libtcc host build, so this tool currently emits
an explicit placeholder measurement document keyed by TU sha256 and a pinned
arena recommendation estimate. Once Phase 3 lands, this script is the single
place to add the real allocator-instrumented measurement path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1
ISSUE = 543
GATING_ISSUE = 408
DEFAULT_ESTIMATED_PEAK_BYTES = 2_621_440  # 2.5 MiB ("several MB" planning estimate)
DEFAULT_TUS = ["tests/m7_toolchain/fixtures/dev/hello.c"]


def round_up_64k(n: int) -> int:
    q = 64 * 1024
    return ((n + (q - 1)) // q) * q


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def detect_phase3_blocker(root: Path) -> str | None:
    makefile = root / "vendor" / "tinycc" / "Makefile.secureos"
    if not makefile.exists():
        return "missing_makefile_secureos"

    text = makefile.read_text(encoding="utf-8", errors="replace")
    if "STATUS: SCAFFOLD" in text:
        return "awaiting_408_phase3"

    submodule = root / "vendor" / "tinycc" / "tinycc"
    if not submodule.exists() or not any(submodule.iterdir()):
        return "tinycc_submodule_not_initialized"

    return None


def build_placeholder_doc(root: Path, tus: list[str], est_peak: int) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    for rel in tus:
        p = root / rel
        if not p.exists():
            entries.append(
                {
                    "tu": rel,
                    "sha256": None,
                    "peak_bytes": None,
                    "status": "missing_tu",
                }
            )
            continue
        entries.append(
            {
                "tu": rel,
                "sha256": sha256_file(p),
                "peak_bytes": None,
                "status": "awaiting_408_phase3",
            }
        )

    recommended = round_up_64k(int(est_peak * 1.5))
    return {
        "_comment": (
            "Issue #543 TinyCC compile-time arena pin. Placeholder mode is "
            "intentional while #408 Phase 3 remains OPEN and Makefile.secureos "
            "is still scaffold-only. Once Phase 3 lands, replace `status` with "
            "`measured`, populate per-TU `peak_bytes`, and keep "
            "`pinned_arena_bytes` as ceil(max_peak * 1.5, 64KiB)."
        ),
        "schemaVersion": SCHEMA_VERSION,
        "issue": ISSUE,
        "gatingIssue": GATING_ISSUE,
        "status": "awaiting_408_phase3",
        "threshold_percent": 5.0,
        "measurements": entries,
        "estimated_peak_bytes": est_peak,
        "pinned_arena_bytes": recommended,
        "pinned_arena_derivation": {
            "mode": "placeholder",
            "formula": "ceil(estimated_peak_bytes * 1.5, 64KiB)",
            "note": (
                "Planning estimate source: plans/2026-05-28-in-os-toolchain-"
                "self-hosting.md (TinyCC needs several MB; runtime.arena_bytes "
                "cap is 16MiB)."
            ),
        },
    }


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    p.add_argument(
        "--tu",
        action="append",
        default=None,
        help="Relative TU path to include (repeatable).",
    )
    p.add_argument(
        "--estimated-peak-bytes",
        type=int,
        default=DEFAULT_ESTIMATED_PEAK_BYTES,
        help="Placeholder per-TU peak estimate used while awaiting #408 Phase 3.",
    )
    p.add_argument(
        "--output",
        default=None,
        help="Write JSON to this path (defaults to stdout).",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    tus = args.tu if args.tu else list(DEFAULT_TUS)

    blocker = detect_phase3_blocker(root)
    if blocker is None:
        # Keep this explicit and deterministic until live-measurement plumbing
        # lands in the same series as #408 Phase 3.
        blocker = "live_measurement_not_implemented"

    doc = build_placeholder_doc(root, tus, args.estimated_peak_bytes)
    doc["status_detail"] = blocker

    payload = json.dumps(doc, indent=2, sort_keys=False) + "\n"
    if args.output:
        out = Path(args.output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
