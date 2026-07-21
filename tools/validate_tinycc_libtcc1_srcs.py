#!/usr/bin/env python3
"""Validate vendor/tinycc/libtcc1-srcs.json (issue #548).

Checks that the pinned required/excluded partition is well-formed and matches
all compilable TU files under vendor/tinycc/tinycc/lib (*.c + *.S).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def emit(line: str) -> None:
    print(line, flush=True)


def fail(reason: str) -> int:
    emit(f"TEST:FAIL:tinycc_libtcc1_srcs:{reason}")
    return 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root",
    )
    parser.add_argument(
        "--manifest",
        default="vendor/tinycc/libtcc1-srcs.json",
        help="Relative path to manifest JSON",
    )
    parser.add_argument(
        "--lib-dir",
        default="vendor/tinycc/tinycc/lib",
        help="Relative path to TinyCC lib directory",
    )
    return parser.parse_args()


def _read_json(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise ValueError(f"manifest_missing:{path}")
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValueError(f"manifest_malformed:{exc.msg}:line={exc.lineno}:col={exc.colno}")
    if not isinstance(data, dict):
        raise ValueError("manifest_top_level_not_object")
    return data


def _validate_entries(doc: dict[str, Any], key: str) -> tuple[set[str], dict[str, str]]:
    value = doc.get(key)
    if not isinstance(value, list) or not value:
        raise ValueError(f"{key}_missing_or_empty")

    files: set[str] = set()
    reasons: dict[str, str] = {}
    for idx, entry in enumerate(value):
        if not isinstance(entry, dict):
            raise ValueError(f"{key}[{idx}]_not_object")
        path = entry.get("path")
        reason = entry.get("reason")
        if not isinstance(path, str) or not path:
            raise ValueError(f"{key}[{idx}]_bad_path")
        if "/" in path or "\\" in path:
            raise ValueError(f"{key}[{idx}]_path_must_be_basename:{path}")
        if not (path.endswith(".c") or path.endswith(".S")):
            raise ValueError(f"{key}[{idx}]_path_not_c_or_S:{path}")
        if not isinstance(reason, str) or not reason.strip():
            raise ValueError(f"{key}[{idx}]_missing_reason:{path}")
        if path in files:
            raise ValueError(f"{key}_duplicate:{path}")
        files.add(path)
        reasons[path] = reason

    return files, reasons


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    manifest_path = (root / args.manifest).resolve()
    lib_dir = (root / args.lib_dir).resolve()

    try:
        doc = _read_json(manifest_path)
    except ValueError as exc:
        return fail(str(exc))

    required_top = [
        "schemaVersion",
        "issue",
        "umbrella",
        "vendorSurfacePin",
        "sourceDir",
        "target",
        "required",
        "excluded",
        "summary",
    ]
    missing = [k for k in required_top if k not in doc]
    if missing:
        return fail(f"manifest_missing_keys:{','.join(missing)}")

    if doc.get("schemaVersion") != 1:
        return fail(f"bad_schema_version:{doc.get('schemaVersion')!r}")
    if doc.get("issue") != 548:
        return fail(f"bad_issue:{doc.get('issue')!r}")
    if doc.get("umbrella") != 408:
        return fail(f"bad_umbrella:{doc.get('umbrella')!r}")
    if doc.get("vendorSurfacePin") != "vendor/tinycc/Makefile.secureos":
        return fail("bad_vendor_surface_pin")

    try:
        required_files, _required_reasons = _validate_entries(doc, "required")
        excluded_files, _excluded_reasons = _validate_entries(doc, "excluded")
    except ValueError as exc:
        return fail(str(exc))

    emit("TEST:PASS:tinycc_libtcc1_srcs_manifest_parses")

    overlap = sorted(required_files & excluded_files)
    if overlap:
        return fail(f"required_excluded_overlap:{','.join(overlap)}")

    if not lib_dir.is_dir():
        return fail(f"lib_dir_missing:{lib_dir}")

    actual_files = {
        p.name for p in lib_dir.iterdir() if p.is_file() and p.suffix in {".c", ".S"}
    }

    missing_required = sorted(required_files - actual_files)
    missing_excluded = sorted(excluded_files - actual_files)
    if missing_required:
        return fail(f"required_entries_missing_in_lib_dir:{','.join(missing_required)}")
    if missing_excluded:
        return fail(f"excluded_entries_missing_in_lib_dir:{','.join(missing_excluded)}")

    emit("TEST:PASS:tinycc_libtcc1_srcs_partition_entries_exist")

    accounted = required_files | excluded_files
    unaccounted = sorted(actual_files - accounted)
    stale = sorted(accounted - actual_files)
    if unaccounted:
        return fail(f"unaccounted_tus:{','.join(unaccounted)}")
    if stale:
        return fail(f"stale_manifest_entries:{','.join(stale)}")

    emit("TEST:PASS:tinycc_libtcc1_srcs_partition_covers_all_tus")

    summary = doc.get("summary")
    if not isinstance(summary, dict):
        return fail("summary_not_object")

    expected_total = summary.get("totalCompilableTus")
    expected_required = summary.get("requiredTus")
    expected_excluded = summary.get("excludedTus")
    if not isinstance(expected_total, int) or not isinstance(expected_required, int) or not isinstance(expected_excluded, int):
        return fail("summary_counts_not_int")

    if expected_total != len(actual_files):
        return fail(f"summary_total_mismatch:expected={expected_total}:live={len(actual_files)}")
    if expected_required != len(required_files):
        return fail(f"summary_required_mismatch:expected={expected_required}:live={len(required_files)}")
    if expected_excluded != len(excluded_files):
        return fail(f"summary_excluded_mismatch:expected={expected_excluded}:live={len(excluded_files)}")

    if expected_required + expected_excluded != expected_total:
        return fail(
            "summary_partition_mismatch:"
            f"required={expected_required}:excluded={expected_excluded}:total={expected_total}"
        )

    emit("TEST:PASS:tinycc_libtcc1_srcs_summary_counts_match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
