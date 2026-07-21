#!/usr/bin/env python3
"""Host drift gate for dev/building.txt (issue #618).

Checks:
1) The "Files here" section in dev/building.txt must match the staged /apps/dev
   paths declared by build/scripts/build_disk_image.sh.
2) If m7_toolchain compile/run markers are still SKIP-pinned (awaiting_*), any
   compile/run recipe in dev/building.txt must be explicitly labeled as gated
   with issue references (default: #408/#409/#410).
3) dev/building.txt and docs/in-os-toolchain/building-apps.md must
   cross-reference each other (including a keep-in-sync reminder).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

CONTRACT_REL = Path("tests/disk_image/dev_building_txt_contract.json")
MAPPING_TOKEN_RE = re.compile(r"[\"']([^\"']+=/[^\"']+)[\"']")
CC_RECIPE_RE = re.compile(r"^\s*cc\s+/apps/dev/[^\s]+\.c\s+-o\s+/apps/[^\s]+\.bin\s*$")


def emit(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def load_json(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"DEV_BUILDING_TXT:FAIL:missing_json:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"DEV_BUILDING_TXT:FAIL:malformed_json:{path}:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err(f"DEV_BUILDING_TXT:FAIL:json_not_object:{path}")
        raise SystemExit(2)
    return raw


def parse_staged_apps_dev_targets(build_script_text: str) -> set[str]:
    staged: set[str] = set()
    for token in MAPPING_TOKEN_RE.findall(build_script_text):
        _, dst = token.split("=", 1)
        dst = dst.strip().rstrip("/")
        if dst.startswith("/apps/dev/"):
            staged.add(dst)
    return staged


def parse_claimed_files_here(guide_text: str) -> set[str]:
    lines = guide_text.splitlines()
    header_idx = next((i for i, line in enumerate(lines) if "files here" in line.lower()), -1)
    if header_idx < 0:
        raise ValueError("files_here_header_missing")

    claimed: set[str] = set()
    for raw in lines[header_idx + 1 :]:
        if not raw.strip():
            break
        if not raw.startswith((" ", "\t")):
            break

        token = raw.strip().split()[0]
        if token.endswith(":"):
            continue
        normalized = token.rstrip("/")
        if not normalized:
            continue
        claimed.add(f"/apps/dev/{normalized}")

    if not claimed:
        raise ValueError("files_here_entries_missing")
    return claimed


def marker_is_awaiting(marker_entry: dict[str, Any]) -> bool:
    reason = marker_entry.get("reason")
    return isinstance(reason, str) and reason.startswith("awaiting_")


def find_marker(markers_doc: dict[str, Any], marker_name: str) -> dict[str, Any] | None:
    markers = markers_doc.get("markers")
    if not isinstance(markers, list):
        return None
    for entry in markers:
        if isinstance(entry, dict) and entry.get("name") == marker_name:
            return entry
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--contract",
        default=str(CONTRACT_REL),
        help="Contract JSON path relative to repo root.",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    contract_path = (root / args.contract).resolve()
    contract = load_json(contract_path)

    failures = 0

    if contract.get("schemaVersion") != 1:
        emit_err(f"DEV_BUILDING_TXT:FAIL:bad_contract_schema:{contract.get('schemaVersion')!r}")
        return 1

    guide_rel = contract.get("guidePath")
    docs_rel = contract.get("docsPath")
    build_rel = contract.get("stagingManifestSource")
    markers_rel = contract.get("markersPath")
    compile_marker_name = contract.get("compileMarker")
    run_marker_name = contract.get("runMarker")
    gate_issues = contract.get("requiredQuickStartGateIssues")

    if not all(isinstance(x, str) for x in [guide_rel, docs_rel, build_rel, markers_rel, compile_marker_name, run_marker_name]):
        emit_err("DEV_BUILDING_TXT:FAIL:contract_fields_invalid")
        return 1
    if not isinstance(gate_issues, list) or not gate_issues or any(not isinstance(i, int) for i in gate_issues):
        emit_err("DEV_BUILDING_TXT:FAIL:required_gate_issues_invalid")
        return 1

    guide_path = root / str(guide_rel)
    docs_path = root / str(docs_rel)
    build_script_path = root / str(build_rel)
    markers_path = root / str(markers_rel)

    for p, label in [
        (guide_path, "guide_missing"),
        (docs_path, "docs_missing"),
        (build_script_path, "staging_manifest_source_missing"),
        (markers_path, "markers_missing"),
    ]:
        if not p.is_file():
            emit_err(f"DEV_BUILDING_TXT:FAIL:{label}:{p}")
            failures += 1

    if failures:
        emit_err(f"DEV_BUILDING_TXT:FAIL:summary:{failures}_preflight_failures")
        return 1

    guide_text = guide_path.read_text(encoding="utf-8")
    docs_text = docs_path.read_text(encoding="utf-8")
    build_script_text = build_script_path.read_text(encoding="utf-8")
    markers_doc = load_json(markers_path)

    # 1) Files-here parity with staged /apps/dev mapping.
    try:
        claimed_paths = parse_claimed_files_here(guide_text)
        emit("DEV_BUILDING_TXT:PASS:files_here_section_parsed")
    except ValueError as exc:
        emit_err(f"DEV_BUILDING_TXT:FAIL:{exc}")
        return 1

    staged_paths = parse_staged_apps_dev_targets(build_script_text)
    missing_from_staging = sorted(claimed_paths - staged_paths)
    missing_from_guide = sorted(staged_paths - claimed_paths)

    if missing_from_staging:
        for p in missing_from_staging:
            emit_err(f"DEV_BUILDING_TXT:FAIL:claimed_but_not_staged:{p}")
        failures += len(missing_from_staging)
    else:
        emit("DEV_BUILDING_TXT:PASS:all_claimed_paths_are_staged")

    if missing_from_guide:
        for p in missing_from_guide:
            emit_err(f"DEV_BUILDING_TXT:FAIL:staged_but_not_claimed:{p}")
        failures += len(missing_from_guide)
    else:
        emit("DEV_BUILDING_TXT:PASS:all_staged_paths_are_claimed")

    # 2) Gating labels for compile/run recipe while markers are awaiting_*.
    compile_marker = find_marker(markers_doc, str(compile_marker_name))
    run_marker = find_marker(markers_doc, str(run_marker_name))
    if compile_marker is None:
        emit_err(f"DEV_BUILDING_TXT:FAIL:missing_compile_marker:{compile_marker_name}")
        failures += 1
    if run_marker is None:
        emit_err(f"DEV_BUILDING_TXT:FAIL:missing_run_marker:{run_marker_name}")
        failures += 1

    guide_lines = guide_text.splitlines()
    has_compile_recipe = any(CC_RECIPE_RE.match(line) for line in guide_lines)
    has_run_recipe = any(line.strip() == "hello" for line in guide_lines)

    gate_issue_refs_ok = all(f"#{issue}" in guide_text for issue in gate_issues)
    gate_phrase_ok = "gated on" in guide_text.lower()

    if compile_marker is not None:
        awaiting_compile = marker_is_awaiting(compile_marker)
        emit(
            "DEV_BUILDING_TXT:INFO:compile_marker_state:"
            f"awaiting={'yes' if awaiting_compile else 'no'}"
        )
        if awaiting_compile and has_compile_recipe and not (gate_issue_refs_ok and gate_phrase_ok):
            emit_err("DEV_BUILDING_TXT:FAIL:compile_recipe_missing_gate_label")
            failures += 1

    if run_marker is not None:
        awaiting_run = marker_is_awaiting(run_marker)
        emit(
            "DEV_BUILDING_TXT:INFO:run_marker_state:"
            f"awaiting={'yes' if awaiting_run else 'no'}"
        )
        if awaiting_run and has_run_recipe and not (gate_issue_refs_ok and gate_phrase_ok):
            emit_err("DEV_BUILDING_TXT:FAIL:run_recipe_missing_gate_label")
            failures += 1

    if gate_issue_refs_ok and gate_phrase_ok:
        emit("DEV_BUILDING_TXT:PASS:quickstart_gate_labels_present")

    # 3) Cross-reference parity.
    if "docs/in-os-toolchain/building-apps.md" in guide_text:
        emit("DEV_BUILDING_TXT:PASS:guide_refs_docs")
    else:
        emit_err("DEV_BUILDING_TXT:FAIL:guide_missing_docs_ref")
        failures += 1

    docs_lower = docs_text.lower()
    if "dev/building.txt" in docs_text and "keep in sync" in docs_lower:
        emit("DEV_BUILDING_TXT:PASS:docs_ref_guide_keep_in_sync")
    else:
        emit_err("DEV_BUILDING_TXT:FAIL:docs_missing_guide_crossref_or_sync_note")
        failures += 1

    if failures:
        emit_err(f"DEV_BUILDING_TXT:FAIL:summary:{failures}_drift_failures")
        return 1

    emit("DEV_BUILDING_TXT:PASS:summary:guide_staging_and_gate_labels_in_sync")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
