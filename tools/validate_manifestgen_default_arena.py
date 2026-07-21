#!/usr/bin/env python3
"""Drift gate for libmanifestgen default runtime.arena_bytes pin (issue #595)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

PIN_REL = Path("tests/manifestgen/default_arena_bytes.json")
HEADER_REL = Path("user/libs/manifestgen/include/manifestgen/manifest_default.h")
SOURCE_REL = Path("user/libs/manifestgen/src/manifest_default.c")
DOC_REL = Path("docs/abi/manifest.md")
SYMBOL = "MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES"
DOC_HEADING = "#### Default `runtime.arena_bytes` policy"


def emit(msg: str) -> None:
    print(msg, flush=True)


def emit_err(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def load_json(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"MANIFESTGEN_DEFAULT_ARENA:FAIL:missing_pin:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"MANIFESTGEN_DEFAULT_ARENA:FAIL:malformed_pin:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err("MANIFESTGEN_DEFAULT_ARENA:FAIL:pin_not_object")
        raise SystemExit(2)
    return raw


def parse_header_default(header_text: str) -> int:
    m = re.search(
        rf"^#define\s+{re.escape(SYMBOL)}\s+([0-9]+)[uU]?\s*$",
        header_text,
        flags=re.MULTILINE,
    )
    if not m:
        raise ValueError(f"missing_define:{SYMBOL}")
    return int(m.group(1), 10)


def doc_mentions_default(doc_text: str, expected: int) -> bool:
    if DOC_HEADING not in doc_text:
        return False
    i = doc_text.index(DOC_HEADING)
    window = doc_text[i : i + 1600]
    if str(expected) not in window:
        return False
    if "cc --manifest <path>" not in window:
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    pin_path = root / PIN_REL
    header_path = root / HEADER_REL
    source_path = root / SOURCE_REL
    doc_path = root / DOC_REL

    pin = load_json(pin_path)
    if pin.get("schemaVersion") != 1:
        emit_err(
            f"MANIFESTGEN_DEFAULT_ARENA:FAIL:bad_schema:{pin.get('schemaVersion')!r}"
        )
        return 1

    constants = pin.get("constants")
    if not isinstance(constants, dict):
        emit_err("MANIFESTGEN_DEFAULT_ARENA:FAIL:constants_missing_or_not_object")
        return 1
    expected = constants.get("runtime_arena_bytes_default")
    if not isinstance(expected, int):
        emit_err("MANIFESTGEN_DEFAULT_ARENA:FAIL:runtime_default_missing_or_not_int")
        return 1

    try:
        header_text = header_path.read_text(encoding="utf-8")
        source_text = source_path.read_text(encoding="utf-8")
        doc_text = doc_path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        emit_err(f"MANIFESTGEN_DEFAULT_ARENA:FAIL:missing_file:{exc.filename}")
        return 2

    try:
        header_default = parse_header_default(header_text)
    except ValueError as exc:
        emit_err(f"MANIFESTGEN_DEFAULT_ARENA:FAIL:header_parse:{exc}")
        return 1

    ok = True

    if expected == header_default:
        emit(f"MANIFESTGEN_DEFAULT_ARENA:PASS:pin_vs_header:{expected}")
    else:
        emit_err(
            "MANIFESTGEN_DEFAULT_ARENA:FAIL:pin_vs_header:"
            f"expected={expected}:actual={header_default}"
        )
        ok = False

    if SYMBOL in source_text:
        emit(f"MANIFESTGEN_DEFAULT_ARENA:PASS:source_uses_symbol:{SYMBOL}")
    else:
        emit_err(f"MANIFESTGEN_DEFAULT_ARENA:FAIL:source_missing_symbol:{SYMBOL}")
        ok = False

    if doc_mentions_default(doc_text, expected):
        emit("MANIFESTGEN_DEFAULT_ARENA:PASS:doc_policy_pinned")
    else:
        emit_err(
            "MANIFESTGEN_DEFAULT_ARENA:FAIL:doc_policy_missing_expected_default_or_precedence"
        )
        ok = False

    if not ok:
        emit_err("MANIFESTGEN_DEFAULT_ARENA:FAIL:summary:pin_drift")
        return 1

    emit("MANIFESTGEN_DEFAULT_ARENA:PASS:summary:pin_consistent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
