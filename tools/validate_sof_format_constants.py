#!/usr/bin/env python3
"""Validate SOF wire-format constants against docs/abi/sof-format-constants.json.

Issue: #547

Compares machine-readable pins in docs/abi/sof-format-constants.json to the
canonical declarations in kernel/format/sof.h. Fails on mismatches, missing
constants, or unexpected extra constants in any pinned group.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

PIN_REL = Path("docs/abi/sof-format-constants.json")
HEADER_REL = Path("kernel/format/sof.h")


def emit(msg: str) -> None:
    print(msg, flush=True)


def emit_err(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def load_json(path: Path) -> dict[str, Any]:
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:missing_pin:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:malformed_pin:{exc}")
        raise SystemExit(2)
    if not isinstance(raw, dict):
        emit_err("SOF_FORMAT_CONSTANTS:FAIL:pin_not_object")
        raise SystemExit(2)
    return raw


def parse_enum_block(header_text: str, typedef_name: str) -> dict[str, int]:
    m = None
    for match in re.finditer(
        r"typedef\s+enum\s*\{(?P<body>.*?)\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;",
        header_text,
        flags=re.DOTALL,
    ):
        if match.group("name") == typedef_name:
            m = match
            break
    if m is None:
        raise ValueError(f"enum typedef not found: {typedef_name}")
    body = m.group("body")

    out: dict[str, int] = {}
    for raw_line in body.splitlines():
        line = raw_line.split("/*", 1)[0].split("//", 1)[0].strip()
        if not line:
            continue
        m_item = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)\s*,?$", line)
        if not m_item:
            continue
        name = m_item.group(1)
        value = int(m_item.group(2), 0)
        out[name] = value

    if not out:
        raise ValueError(f"enum typedef parsed empty: {typedef_name}")
    return out


def parse_named_const(header_text: str, name: str) -> int:
    m = re.search(rf"\b{re.escape(name)}\b\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)", header_text)
    if not m:
        raise ValueError(f"constant not found: {name}")
    return int(m.group(1), 0)


def parse_header_size(header_text: str) -> int:
    m = re.search(
        r"_Static_assert\s*\(\s*sizeof\(\s*sof_header_t\s*\)\s*==\s*([0-9]+)",
        header_text,
    )
    if not m:
        raise ValueError("sof_header_t static assert size not found")
    return int(m.group(1), 10)


def parse_magic_ascii(header_text: str) -> str:
    m = re.search(r'magic\[4\][^\n]*"([A-Za-z0-9]{4})"', header_text)
    if not m:
        raise ValueError("magic ASCII comment not found")
    return m.group(1)


def parse_format_version(header_text: str) -> int:
    m = re.search(r"format_version[^\n]*/\*\s*([0-9]+)\s*\*/", header_text)
    if not m:
        raise ValueError("format_version comment not found")
    return int(m.group(1), 10)


def compare_scalar(name: str, expected: Any, actual: Any) -> bool:
    if expected == actual:
        emit(f"SOF_FORMAT_CONSTANTS:PASS:{name}:{actual}")
        return True
    emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:{name}:expected={expected!r}:actual={actual!r}")
    return False


def compare_mapping(group: str, expected: dict[str, int], actual: dict[str, int]) -> bool:
    ok = True

    for key in sorted(expected.keys() - actual.keys()):
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:{group}:missing:{key}:expected={expected[key]}")
        ok = False

    for key in sorted(actual.keys() - expected.keys()):
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:{group}:unexpected:{key}:actual={actual[key]}")
        ok = False

    for key in sorted(expected.keys() & actual.keys()):
        if expected[key] == actual[key]:
            emit(f"SOF_FORMAT_CONSTANTS:PASS:{group}:{key}:{actual[key]}")
        else:
            emit_err(
                f"SOF_FORMAT_CONSTANTS:FAIL:{group}:{key}:expected={expected[key]}:actual={actual[key]}"
            )
            ok = False

    return ok


def build_actual_constants(header_text: str) -> dict[str, Any]:
    return {
        "magic_ascii": parse_magic_ascii(header_text),
        "format_version": parse_format_version(header_text),
        "header_size_bytes": parse_header_size(header_text),
        "file_types": parse_enum_block(header_text, "sof_file_type_t"),
        "signature_algorithms": parse_enum_block(header_text, "sof_sig_algorithm_t"),
        "metadata_keys": parse_enum_block(header_text, "sof_meta_key_t"),
        "result_codes": parse_enum_block(header_text, "sof_result_t"),
        "limits": {
            "SOF_META_VALUE_MAX": parse_named_const(header_text, "SOF_META_VALUE_MAX"),
            "SOF_META_MAX_ENTRIES": parse_named_const(header_text, "SOF_META_MAX_ENTRIES"),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root (defaults to parent of tools/).",
    )
    parser.add_argument(
        "--pin",
        default=str(PIN_REL),
        help="Path to JSON pin (relative to --root when not absolute).",
    )
    parser.add_argument(
        "--header",
        default=str(HEADER_REL),
        help="Path to sof.h header (relative to --root when not absolute).",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    pin_path = Path(args.pin)
    header_path = Path(args.header)
    if not pin_path.is_absolute():
        pin_path = root / pin_path
    if not header_path.is_absolute():
        header_path = root / header_path

    pin = load_json(pin_path)
    if pin.get("schemaVersion") != 1:
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:bad_schema:{pin.get('schemaVersion')!r}")
        return 1

    constants = pin.get("constants")
    if not isinstance(constants, dict):
        emit_err("SOF_FORMAT_CONSTANTS:FAIL:constants_missing_or_not_object")
        return 1

    try:
        header_text = header_path.read_text(encoding="utf-8")
    except FileNotFoundError:
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:missing_header:{header_path}")
        return 2

    try:
        actual = build_actual_constants(header_text)
    except ValueError as exc:
        emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:parse_error:{exc}")
        return 2

    ok = True
    for scalar in ("magic_ascii", "format_version", "header_size_bytes"):
        if scalar not in constants:
            emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:missing_pin_scalar:{scalar}")
            ok = False
            continue
        ok = compare_scalar(scalar, constants[scalar], actual[scalar]) and ok

    for group in (
        "file_types",
        "signature_algorithms",
        "metadata_keys",
        "result_codes",
        "limits",
    ):
        expected_group = constants.get(group)
        if not isinstance(expected_group, dict):
            emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:missing_or_bad_pin_group:{group}")
            ok = False
            continue
        if not all(isinstance(k, str) and isinstance(v, int) for k, v in expected_group.items()):
            emit_err(f"SOF_FORMAT_CONSTANTS:FAIL:pin_group_non_int_values:{group}")
            ok = False
            continue
        ok = compare_mapping(group, expected_group, actual[group]) and ok

    if not ok:
        emit_err("SOF_FORMAT_CONSTANTS:FAIL:summary:pin_mismatch")
        return 1

    emit("SOF_FORMAT_CONSTANTS:PASS:summary:all_constants_match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
