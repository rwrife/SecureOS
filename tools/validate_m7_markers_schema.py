#!/usr/bin/env python3
# tools/validate_m7_markers_schema.py
#
# Issue #611: schema drift gate for tests/m7_toolchain/markers.json rows.
#
# This validator is intentionally additive with the existing
# tools/validate_m7_markers.py gate: it enforces row-shape contracts for
# metadata fields without replacing marker wiring / issue-state checks.

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

MARKER = "M7_SCHEMA"

RE_ID = re.compile(r"^toolchain_[a-z0-9_]+$")
RE_ADDED_IN = re.compile(r"^(issue|pr|commit)-[A-Za-z0-9._-]+$")
SKIP_REASON_ENUM = {"pre-408", "pre-409", "pre-410", "pre-585", "deferred"}

# Keep legacy fields (`name`, `reason`, `description`) for compatibility with
# existing gates/scripts while adding schema-pinned metadata (`id`,
# `harnessPath`, `skipReason`, `addedIn`).
REQUIRED_ROW_KEYS = {
    "id",
    "name",
    "harnessPath",
    "gatingIssue",
    "reason",
    "skipReason",
    "addedIn",
    "description",
}
OPTIONAL_ROW_KEYS = {"gatingIssues"}


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def die_usage(reason: str) -> int:
    emit_err(f"{MARKER}:FAIL:{reason}")
    return 2


def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        raise ValueError(f"missing_markers_json:{path}")
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(f"malformed_markers_json:{exc}") from exc
    if not isinstance(doc, dict):
        raise ValueError("invalid_markers_root:not_object")
    return doc


def _validate_gating_issues(row: dict[str, Any], row_label: str) -> list[str]:
    failures: list[str] = []

    gating_issue = row.get("gatingIssue")
    if not isinstance(gating_issue, int):
        failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssue_not_integer:{gating_issue!r}")
        return failures
    if gating_issue <= 0:
        failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssue_not_positive:{gating_issue}")

    gating_issues = row.get("gatingIssues")
    if gating_issues is None:
        return failures

    if not isinstance(gating_issues, list) or not gating_issues:
        failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssues_not_nonempty_list")
        return failures

    seen: set[int] = set()
    for idx, value in enumerate(gating_issues):
        if not isinstance(value, int):
            failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssues_non_integer:{idx}:{value!r}")
            continue
        if value <= 0:
            failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssues_not_positive:{idx}:{value}")
            continue
        if value in seen:
            failures.append(f"{MARKER}:FAIL:{row_label}:gatingIssues_duplicate:{value}")
            continue
        seen.add(value)

    if isinstance(gating_issue, int) and gating_issue > 0 and gating_issue not in seen:
        failures.append(
            f"{MARKER}:FAIL:{row_label}:gatingIssues_missing_primary:{gating_issue}"
        )

    return failures


def validate_rows(doc: dict[str, Any], root: Path) -> tuple[list[str], int]:
    failures: list[str] = []

    schema_version = doc.get("schemaVersion")
    if schema_version != 1:
        failures.append(f"{MARKER}:FAIL:unsupported_schema_version:{schema_version!r}")

    rows = doc.get("markers")
    if not isinstance(rows, list):
        failures.append(f"{MARKER}:FAIL:invalid_markers_list")
        return failures, 0

    seen_ids: set[str] = set()

    for idx, row in enumerate(rows):
        row_label = f"row_{idx}"

        if not isinstance(row, dict):
            failures.append(f"{MARKER}:FAIL:{row_label}:not_object")
            continue

        keys = set(row.keys())
        missing = sorted(REQUIRED_ROW_KEYS - keys)
        extra = sorted(keys - REQUIRED_ROW_KEYS - OPTIONAL_ROW_KEYS)
        if missing:
            failures.append(f"{MARKER}:FAIL:{row_label}:missing_keys:{','.join(missing)}")
        if extra:
            failures.append(f"{MARKER}:FAIL:{row_label}:extra_keys:{','.join(extra)}")

        marker_id = row.get("id")
        if not isinstance(marker_id, str) or not RE_ID.match(marker_id):
            failures.append(f"{MARKER}:FAIL:{row_label}:invalid_id:{marker_id!r}")
        elif marker_id in seen_ids:
            failures.append(f"{MARKER}:FAIL:{row_label}:duplicate_id:{marker_id}")
        else:
            seen_ids.add(marker_id)

        marker_name = row.get("name")
        if not isinstance(marker_name, str) or not RE_ID.match(marker_name):
            failures.append(f"{MARKER}:FAIL:{row_label}:invalid_name:{marker_name!r}")
        elif isinstance(marker_id, str) and marker_name != marker_id:
            failures.append(f"{MARKER}:FAIL:{row_label}:id_name_mismatch:{marker_id}:{marker_name}")

        failures.extend(_validate_gating_issues(row, row_label))

        skip_reason = row.get("skipReason")
        if not isinstance(skip_reason, str) or skip_reason not in SKIP_REASON_ENUM:
            failures.append(
                f"{MARKER}:FAIL:{row_label}:invalid_skipReason:{skip_reason!r}:expected_one_of:{'|'.join(sorted(SKIP_REASON_ENUM))}"
            )

        added_in = row.get("addedIn")
        if not isinstance(added_in, str) or not RE_ADDED_IN.match(added_in):
            failures.append(
                f"{MARKER}:FAIL:{row_label}:invalid_addedIn:{added_in!r}:expected_prefix_issue-pr-or-commit"
            )

        harness_path = row.get("harnessPath")
        if not isinstance(harness_path, str) or not harness_path:
            failures.append(f"{MARKER}:FAIL:{row_label}:invalid_harnessPath:{harness_path!r}")
        else:
            p = Path(harness_path)
            if p.is_absolute():
                failures.append(f"{MARKER}:FAIL:{row_label}:harnessPath_absolute:{harness_path}")
            else:
                normalized = p.as_posix()
                if not normalized.startswith("tests/m7_toolchain/"):
                    failures.append(
                        f"{MARKER}:FAIL:{row_label}:harnessPath_outside_m7_toolchain:{harness_path}"
                    )
                if p.suffix not in {".sh", ".c", ".py"}:
                    failures.append(
                        f"{MARKER}:FAIL:{row_label}:harnessPath_bad_suffix:{harness_path}"
                    )
                full = (root / p).resolve()
                try:
                    full.relative_to(root)
                except ValueError:
                    failures.append(
                        f"{MARKER}:FAIL:{row_label}:harnessPath_escapes_repo:{harness_path}"
                    )
                else:
                    if not full.exists():
                        failures.append(
                            f"{MARKER}:FAIL:{row_label}:harnessPath_missing_on_disk:{harness_path}"
                        )

    return failures, len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate tests/m7_toolchain/markers.json row schema for issue #611."
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
    args = parser.parse_args()

    root = Path(args.root).resolve()
    markers_path = Path(args.markers)
    if not markers_path.is_absolute():
        markers_path = root / markers_path

    try:
        doc = load_json(markers_path)
    except ValueError as exc:
        return die_usage(str(exc))

    failures, checked = validate_rows(doc, root)
    if failures:
        for line in failures:
            emit_err(line)
        emit_err(f"{MARKER}:FAIL:summary:{len(failures)}_failures")
        return 1

    emit_out(f"{MARKER}:PASS:summary:{checked}_rows_valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
