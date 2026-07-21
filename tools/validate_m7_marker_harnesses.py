#!/usr/bin/env python3
# tools/validate_m7_marker_harnesses.py
#
# Issue #604: fail when tests/m7_toolchain/markers.json gains a marker row
# without a sibling harness file on disk.
#
# Contract (per marker name):
#   PASS if one of:
#     - tests/m7_toolchain/<name>.sh exists
#     - tests/m7_toolchain/<name>.c exists
#     - name appears in allowlist with justification
#   FAIL otherwise, with deterministic diagnostics naming expected paths.

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

MARKER = "M7_HARNESS"


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def load_markers(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        emit_err(f"{MARKER}:FAIL:missing_markers_json:{path}")
        sys.exit(2)
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"{MARKER}:FAIL:malformed_markers_json:{exc}")
        sys.exit(2)

    rows = doc.get("markers")
    if not isinstance(rows, list):
        emit_err(f"{MARKER}:FAIL:invalid_markers_list")
        sys.exit(2)

    out: list[dict[str, Any]] = []
    for idx, row in enumerate(rows):
        if not isinstance(row, dict):
            emit_err(f"{MARKER}:FAIL:invalid_marker_row:{idx}")
            sys.exit(2)
        name = row.get("name")
        if not isinstance(name, str) or not name.startswith("toolchain_"):
            emit_err(f"{MARKER}:FAIL:invalid_marker_name:{idx}:{name!r}")
            sys.exit(2)
        out.append(row)
    return out


def load_allowlist(path: Path) -> dict[str, str]:
    if not path.exists():
        emit_err(f"{MARKER}:FAIL:missing_allowlist_json:{path}")
        sys.exit(2)
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit_err(f"{MARKER}:FAIL:malformed_allowlist_json:{exc}")
        sys.exit(2)

    if doc.get("schemaVersion") != 1:
        emit_err(f"{MARKER}:FAIL:unsupported_allowlist_schema:{doc.get('schemaVersion')!r}")
        sys.exit(2)

    entries = doc.get("entries")
    if not isinstance(entries, list):
        emit_err(f"{MARKER}:FAIL:invalid_allowlist_entries")
        sys.exit(2)

    out: dict[str, str] = {}
    for idx, row in enumerate(entries):
        if not isinstance(row, dict):
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_row:{idx}")
            sys.exit(2)
        name = row.get("name")
        justification = row.get("justification")
        if not isinstance(name, str) or not name:
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_name:{idx}:{name!r}")
            sys.exit(2)
        if not isinstance(justification, str) or not justification.strip():
            emit_err(f"{MARKER}:FAIL:invalid_allowlist_justification:{idx}:{name!r}")
            sys.exit(2)
        if name in out:
            emit_err(f"{MARKER}:FAIL:duplicate_allowlist_name:{name}")
            sys.exit(2)
        out[name] = justification.strip()

    return out


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Assert every tests/m7_toolchain marker has a sibling harness file."
    )
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--markers",
        default="tests/m7_toolchain/markers.json",
        help="Path (relative to --root or absolute) to markers.json",
    )
    parser.add_argument(
        "--allowlist",
        default="tests/m7_toolchain/marker_harness_allowlist.json",
        help="Path (relative to --root or absolute) to harness allowlist JSON",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    markers_path = Path(args.markers)
    allowlist_path = Path(args.allowlist)

    if not markers_path.is_absolute():
        markers_path = root / markers_path
    if not allowlist_path.is_absolute():
        allowlist_path = root / allowlist_path

    markers = load_markers(markers_path)
    allowlist = load_allowlist(allowlist_path)

    failures = 0
    marker_names: set[str] = set()

    tests_dir = root / "tests" / "m7_toolchain"

    for row in markers:
        name = str(row["name"])
        marker_names.add(name)

        sh_path = tests_dir / f"{name}.sh"
        c_path = tests_dir / f"{name}.c"

        if sh_path.exists():
            emit_out(f"{MARKER}:PASS:{name}:harness_present:{sh_path.relative_to(root)}")
            continue
        if c_path.exists():
            emit_out(f"{MARKER}:PASS:{name}:harness_present:{c_path.relative_to(root)}")
            continue

        justification = allowlist.get(name)
        if justification is not None:
            emit_out(f"{MARKER}:PASS:{name}:allowlisted:{justification}")
            continue

        emit_err(
            f"{MARKER}:FAIL:{name}:missing_harness:expected_one_of:{sh_path.relative_to(root)}|{c_path.relative_to(root)}"
        )
        failures += 1

    stale_allowlist = sorted(set(allowlist.keys()) - marker_names)
    for name in stale_allowlist:
        emit_err(f"{MARKER}:FAIL:{name}:stale_allowlist_entry")
        failures += 1

    if failures:
        emit_err(f"{MARKER}:FAIL:summary:{failures}_failures")
        return 1

    emit_out(f"{MARKER}:PASS:summary:{len(marker_names)}_markers_checked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
