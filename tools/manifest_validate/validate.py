#!/usr/bin/env python3
"""
SecureOS manifest validator (stdlib-only).

Validates one or more `*.manifest.json` files against a JSON-Schema 2020-12
manifest schema (today: `manifests/schema/v0.json`). Implements only the
subset of JSON-Schema keywords actually used by that schema so the
toolchain image does not need an extra pip dependency. See follow-up
issue #195 / BUILD_ROADMAP §7.

Exit codes:
  0  every input validated
  1  validation failure (CLI prints offending file + JSON pointer + rule)
  2  usage / I/O error

Supported keywords: type, const, enum, required, properties,
additionalProperties (false only), items (object), pattern, minLength,
maxLength, description ($schema, $id, title are ignored). Anything else
in the schema is treated as a hard failure so we never silently let
unknown rules through.
"""

from __future__ import annotations

import argparse
import glob
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable

KNOWN_KEYWORDS = {
    "$schema",
    "$id",
    "title",
    "description",
    "type",
    "const",
    "enum",
    "required",
    "properties",
    "additionalProperties",
    "items",
    "pattern",
    "minLength",
    "maxLength",
}


class ValidationError(Exception):
    def __init__(self, pointer: str, message: str) -> None:
        super().__init__(f"{pointer}: {message}")
        self.pointer = pointer
        self.message = message


def _type_matches(value: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "null":
        return value is None
    raise ValidationError("/", f"unsupported schema type: {expected!r}")


def _check_unknown_keywords(schema: dict, pointer: str) -> None:
    extra = set(schema) - KNOWN_KEYWORDS
    if extra:
        raise ValidationError(
            pointer,
            "schema uses unsupported keyword(s): " + ", ".join(sorted(extra)),
        )


def _validate(value: Any, schema: dict, pointer: str = "") -> None:
    _check_unknown_keywords(schema, pointer)

    if "type" in schema:
        if not _type_matches(value, schema["type"]):
            raise ValidationError(
                pointer or "/",
                f"expected type {schema['type']!r}, got {type(value).__name__}",
            )

    if "const" in schema and value != schema["const"]:
        raise ValidationError(
            pointer or "/", f"expected const {schema['const']!r}, got {value!r}"
        )

    if "enum" in schema and value not in schema["enum"]:
        raise ValidationError(
            pointer or "/",
            f"value {value!r} not in enum {schema['enum']!r}",
        )

    if isinstance(value, str):
        if "pattern" in schema and not re.search(schema["pattern"], value):
            raise ValidationError(
                pointer or "/",
                f"string {value!r} does not match pattern {schema['pattern']!r}",
            )
        if "minLength" in schema and len(value) < schema["minLength"]:
            raise ValidationError(
                pointer or "/", f"string shorter than minLength {schema['minLength']}"
            )
        if "maxLength" in schema and len(value) > schema["maxLength"]:
            raise ValidationError(
                pointer or "/", f"string longer than maxLength {schema['maxLength']}"
            )

    if isinstance(value, dict):
        if "required" in schema:
            for field in schema["required"]:
                if field not in value:
                    raise ValidationError(
                        pointer or "/", f"missing required field {field!r}"
                    )

        props = schema.get("properties", {})
        if schema.get("additionalProperties", True) is False:
            for field in value:
                if field not in props:
                    raise ValidationError(
                        f"{pointer}/{field}",
                        "unknown field (additionalProperties: false)",
                    )

        for field, subschema in props.items():
            if field in value:
                _validate(value[field], subschema, f"{pointer}/{field}")

    if isinstance(value, list) and "items" in schema:
        for idx, item in enumerate(value):
            _validate(item, schema["items"], f"{pointer}/{idx}")


def validate_file(manifest_path: Path, schema: dict) -> list[ValidationError]:
    try:
        with manifest_path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except json.JSONDecodeError as exc:
        return [ValidationError("/", f"invalid JSON: {exc}")]

    try:
        _validate(data, schema, "")
    except ValidationError as exc:
        return [exc]
    return []


def _expand_globs(patterns: Iterable[str]) -> list[Path]:
    out: list[Path] = []
    for pattern in patterns:
        matches = sorted(Path(p) for p in glob.glob(pattern, recursive=True))
        if not matches and not any(c in pattern for c in "*?[]"):
            matches = [Path(pattern)]
        out.extend(matches)
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SecureOS manifest schema validator")
    parser.add_argument(
        "--schema",
        required=True,
        help="Path to JSON-Schema file (e.g. manifests/schema/v0.json).",
    )
    parser.add_argument(
        "manifests",
        nargs="+",
        help="One or more manifest paths or globs (e.g. 'manifests/examples/*.manifest.json').",
    )
    args = parser.parse_args(argv)

    schema_path = Path(args.schema)
    try:
        with schema_path.open("r", encoding="utf-8") as handle:
            schema = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"manifest-validate: cannot read schema {schema_path}: {exc}", file=sys.stderr)
        return 2

    manifest_paths = _expand_globs(args.manifests)
    if not manifest_paths:
        print("manifest-validate: no manifest files matched", file=sys.stderr)
        return 2

    failures = 0
    for path in manifest_paths:
        errors = validate_file(path, schema)
        if not errors:
            print(f"PASS {path}")
            continue
        failures += 1
        for err in errors:
            print(f"FAIL {path} {err.pointer or '/'} {err.message}")

    if failures:
        print(
            f"manifest-validate: {failures}/{len(manifest_paths)} manifest(s) failed",
            file=sys.stderr,
        )
        return 1

    print(f"manifest-validate: {len(manifest_paths)} manifest(s) OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
