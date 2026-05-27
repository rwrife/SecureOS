#!/usr/bin/env python3
"""validate_sosh_capability_contract.py — issue #351 (drift validator slice)

Asserts that every `CAP_*` id referenced in §4 of
`docs/abi/sosh-capability-contract.md` ("Side-effecting builtins +
required capabilities") is consistent with the canonical
`docs/abi/capability-registry.json`. This is the same pattern the rest
of the validators use (#234 registry validator, #297 ABI-stamp drift):
fail loudly the moment the doc drifts from the registry so an
enforcement-slice PR (e.g. PR #358) cannot quietly land a gate against a
cap that does not exist.

What it checks:

  1. Every `CAP_*` appearing in a §4 table row is one of:
        a. present in `capability-registry.json`, OR
        b. annotated `(if defined)` on the same row — the contract
           §4 footnote explicitly carves this out (today this is only
           `CAP_ENV_WRITE`), in which case the validator MUST tolerate
           the cap being absent from the registry, but if it IS in the
           registry it still has to round-trip (the annotation does not
           give the doc a free pass to misspell the id).
  2. The deny-marker `name` field on each row (the second segment of
     the `CAP:DENY:<sid>:<name>:<resource>` literal) matches the
     `deny_marker` template recorded in the registry entry for the
     same cap. Catches the drift mode where a renamed capability gets
     updated in the registry but the contract still cites the old
     marker spelling (or vice versa).

Output contract (mirrors `validate_capability_registry.py` markers
exactly so the existing test-marker scrapers and #234 grep harness can
classify failures without parsing English text):

  * On success per row: `SOSH_CONTRACT:PASS:<cap_id>:<marker_name>`
  * On overall pass:    `SOSH_CONTRACT:PASS:overall`
  * On per-row failure: `SOSH_CONTRACT:FAIL:<reason>:<cap_id>[:<detail>]`

Exit codes:
  0  every §4 row passes both checks
  1  one or more `SOSH_CONTRACT:FAIL:*` markers emitted
  2  environment / usage error (missing input file, malformed JSON,
     contract doc lacks a §4 table)

Wired by:
  * `build/scripts/validate_sosh_capability_contract.sh`  (parity wrapper)
  * `build/scripts/test.sh sosh_contract_registry_drift`  (#156 parity)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# A §4 table row carries pipe-delimited cells. We only look at rows that
# actually cite a CAP_* id in the "Required cap" column (3rd cell). The
# first regex pulls the cap id; the second pulls the canonical marker
# template from the "Deny marker" column (4th cell). We use simple text
# scans rather than a markdown parser so the validator stays
# dependency-free (matches the rest of tools/*.py).
CAP_ID_IN_CELL_RE = re.compile(r"\bCAP_[A-Z][A-Z0-9_]*\b")
MARKER_IN_CELL_RE = re.compile(r"`CAP:DENY:[^`]+`")
IF_DEFINED_RE = re.compile(r"\(if\s+defined\)", re.IGNORECASE)
SECTION_4_HEADER_RE = re.compile(
    r"^##\s*4\.\s*Side-effecting builtins", re.IGNORECASE)
NEXT_SECTION_RE = re.compile(r"^##\s*\d+\.")
TABLE_ROW_RE = re.compile(r"^\|")
TABLE_SEPARATOR_RE = re.compile(r"^\|\s*-+")
# Marker template in the registry is "CAP:DENY:<actor_subject_id>:<name>:<resource>".
# We only care about the <name> segment (the lowercase identifier between
# the second and third colons).
MARKER_TEMPLATE_NAME_RE = re.compile(
    r"^CAP:DENY:[^:]+:([a-z][a-z0-9_]*):")


def emit(line: str) -> None:
    print(line, flush=True)


def err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def load_registry(path: Path) -> Dict[str, dict]:
    try:
        with path.open("r", encoding="utf-8") as fh:
            data = json.load(fh)
    except FileNotFoundError:
        err(f"SOSH_CONTRACT:FAIL:registry_missing:{path}")
        sys.exit(2)
    except json.JSONDecodeError as exc:
        err(f"SOSH_CONTRACT:FAIL:registry_malformed:{path}:{exc}")
        sys.exit(2)
    caps = data.get("capabilities", [])
    out: Dict[str, dict] = {}
    for entry in caps:
        cap_id = entry.get("cap_id")
        if isinstance(cap_id, str):
            out[cap_id] = entry
    return out


def extract_section_4_rows(doc_path: Path) -> List[str]:
    """Return the raw text of every table row inside §4 of the contract.

    Excludes the header row and the `|---|---|` separator row so the
    caller can iterate cleanly.
    """
    try:
        text = doc_path.read_text(encoding="utf-8")
    except FileNotFoundError:
        err(f"SOSH_CONTRACT:FAIL:doc_missing:{doc_path}")
        sys.exit(2)

    lines = text.splitlines()
    in_section = False
    rows: List[str] = []
    header_consumed = False
    separator_consumed = False
    saw_table = False

    for raw in lines:
        if SECTION_4_HEADER_RE.match(raw):
            in_section = True
            continue
        if not in_section:
            continue
        if NEXT_SECTION_RE.match(raw):
            break
        if not TABLE_ROW_RE.match(raw):
            # A blank line or prose line between rows means the table
            # is over; stop collecting (notes/footnotes follow).
            if rows and raw.strip() == "":
                # Allow trailing prose; stop scanning rows once we have
                # at least one row and hit a non-table line.
                break
            continue
        saw_table = True
        if not header_consumed:
            header_consumed = True
            continue
        if not separator_consumed and TABLE_SEPARATOR_RE.match(raw):
            separator_consumed = True
            continue
        rows.append(raw)

    if not saw_table:
        err(f"SOSH_CONTRACT:FAIL:doc_no_section_4_table:{doc_path}")
        sys.exit(2)
    return rows


def parse_row(row: str) -> Tuple[List[str], bool, Optional[str]]:
    """Split a table row into (cap_ids, if_defined, marker_name).

    `cap_ids` is every CAP_* literal cited anywhere in the row (we
    want all of them — e.g. `CAP_FS_READ` appears in the same cell as
    `os_fs_read_file` in some rows). `if_defined` reflects whether the
    row contains the `(if defined)` annotation. `marker_name` is the
    <name> segment of the deny-marker literal in the 4th cell (or
    None if no marker literal is present).
    """
    cap_ids = CAP_ID_IN_CELL_RE.findall(row)
    if_defined = bool(IF_DEFINED_RE.search(row))
    marker_match = MARKER_IN_CELL_RE.search(row)
    marker_name: Optional[str] = None
    if marker_match:
        literal = marker_match.group(0).strip("`")
        parts = literal.split(":")
        # parts == ['CAP', 'DENY', '<sid>', '<name>', '<resource>'...]
        if len(parts) >= 4:
            marker_name = parts[3]
    return cap_ids, if_defined, marker_name


def registry_marker_name(entry: dict) -> Optional[str]:
    template = entry.get("deny_marker", "")
    match = MARKER_TEMPLATE_NAME_RE.match(template)
    return match.group(1) if match else None


def check(root: Path) -> int:
    doc_path = root / "docs" / "abi" / "sosh-capability-contract.md"
    registry_path = root / "docs" / "abi" / "capability-registry.json"

    registry = load_registry(registry_path)
    rows = extract_section_4_rows(doc_path)

    failures = 0
    checked = 0
    for row in rows:
        cap_ids, if_defined, marker_name = parse_row(row)
        if not cap_ids:
            # Rows without any CAP_* are not capability-bearing and
            # therefore have nothing to drift-check against the
            # registry (defensive: today every §4 row cites at least
            # one cap, but we don't want to fail on a future
            # documentation-only row).
            continue
        for cap_id in cap_ids:
            checked += 1
            entry = registry.get(cap_id)
            if entry is None:
                if if_defined:
                    emit(f"SOSH_CONTRACT:PASS:{cap_id}:if_defined_absent_ok")
                    continue
                err(f"SOSH_CONTRACT:FAIL:cap_missing_from_registry:{cap_id}")
                failures += 1
                continue
            # Cap IS in the registry. Check the marker-name round-trip
            # only when this row actually cites a marker literal — some
            # rows (e.g. the env_write row when CAP_ENV_WRITE is
            # tentative) still want to be drift-checked even if the
            # marker shape is provisional.
            if marker_name is None:
                emit(f"SOSH_CONTRACT:PASS:{cap_id}:row_no_marker")
                continue
            registry_name = registry_marker_name(entry)
            if registry_name is None:
                err(
                    f"SOSH_CONTRACT:FAIL:registry_marker_malformed:"
                    f"{cap_id}:{entry.get('deny_marker', '')}")
                failures += 1
                continue
            if marker_name != registry_name:
                err(
                    f"SOSH_CONTRACT:FAIL:marker_name_mismatch:"
                    f"{cap_id}:doc={marker_name}:registry={registry_name}")
                failures += 1
                continue
            emit(f"SOSH_CONTRACT:PASS:{cap_id}:{marker_name}")

    if checked == 0:
        err("SOSH_CONTRACT:FAIL:no_cap_ids_extracted_from_section_4")
        return 1
    if failures:
        return 1
    emit("SOSH_CONTRACT:PASS:overall")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", default=".",
        help="Repository root (defaults to current directory).")
    args = parser.parse_args()
    return check(Path(args.root).resolve())


if __name__ == "__main__":
    sys.exit(main())
