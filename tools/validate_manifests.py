#!/usr/bin/env python3
"""
validate_manifests.py — Validate SecureOS app manifests against the
manifest JSON Schema (manifests/schema/v0.json).

Issue #195: lock in the value of the schema landed by #187 by re-validating
every in-tree example manifest on every PR. Without this, schema and examples
silently drift — exactly the ABI-rot mode BUILD_ROADMAP §7 is meant to
prevent.

Usage:
    tools/validate_manifests.py [--schema PATH] [--root PATH] [GLOB ...]

Defaults:
    --schema  manifests/schema/v0.json
    --root    repository root (parent of `tools/`)
    GLOB(s)   manifests/examples/*.json

Exit codes:
    0  — every manifest validated against the schema
    1  — one or more manifests failed validation (errors printed with the
         offending file + JSON-pointer + message)
    2  — usage / environment error (missing schema, missing jsonschema lib,
         no manifests matched)
"""

from __future__ import annotations

import argparse
import glob as globlib
import json
import os
import sys
from pathlib import Path


def _fail(msg: str, code: int = 2) -> "int":
    print(f"MANIFEST_VALIDATE:ERROR:{msg}", file=sys.stderr)
    return code


def _load_json(path: Path) -> object:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


# Matches:  #define OS_ABI_VERSION_MAJOR 0
# Tolerant of extra whitespace and trailing comments; rejects token-paste,
# function-like macros, or non-integer right-hand sides so a future
# refactor of the header does not silently start returning the wrong value.
_ABI_MAJOR_RE = __import__("re").compile(
    r"^\s*#\s*define\s+OS_ABI_VERSION_MAJOR\s+(\d+)\s*(?:/[/*].*)?$"
)


def _parse_abi_major_from_header(header_path: Path) -> "int | None":
    """Parse `#define OS_ABI_VERSION_MAJOR <int>` out of secureos_abi.h.

    Returns None if the header is missing, unreadable, or does not contain
    a parseable definition. Callers convert that into a usage error.
    """
    try:
        text = header_path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        m = _ABI_MAJOR_RE.match(line)
        if m:
            return int(m.group(1))
    return None


def _format_pointer(absolute_path) -> str:
    # jsonschema's ValidationError.absolute_path is a deque of keys/indices.
    # Emit RFC 6901 JSON-pointer so failures are unambiguous in CI logs.
    parts = []
    for p in absolute_path:
        if isinstance(p, int):
            parts.append(str(p))
        else:
            # escape '~' -> '~0' and '/' -> '~1' per RFC 6901.
            parts.append(str(p).replace("~", "~0").replace("/", "~1"))
    return "/" + "/".join(parts) if parts else ""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Validate SecureOS app manifests against the v0 schema."
    )
    parser.add_argument(
        "--schema",
        default=None,
        help="Path to the JSON Schema (default: manifests/schema/v0.json).",
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Repository root used to resolve relative paths (default: auto).",
    )
    parser.add_argument(
        "globs",
        nargs="*",
        help="Glob(s) for manifests to validate "
        "(default: manifests/examples/*.json).",
    )
    parser.add_argument(
        "--require-abi-major",
        type=int,
        default=None,
        help="Require each manifest's os_abi_version to equal this integer "
        "(issue #227). Use --require-abi-major-from-header to derive it "
        "automatically from secureos_abi.h.",
    )
    parser.add_argument(
        "--require-abi-major-from-header",
        default=None,
        help="Path to secureos_abi.h; parses OS_ABI_VERSION_MAJOR out of it "
        "and uses that as --require-abi-major. Relative paths resolve "
        "against --root. Overrides --require-abi-major if both are given.",
    )
    args = parser.parse_args(argv)

    if args.root:
        root = Path(args.root).resolve()
    else:
        root = Path(__file__).resolve().parent.parent

    require_abi_major: int | None = args.require_abi_major
    if args.require_abi_major_from_header:
        header_path = Path(args.require_abi_major_from_header)
        if not header_path.is_absolute():
            header_path = (root / header_path).resolve()
        parsed = _parse_abi_major_from_header(header_path)
        if parsed is None:
            return _fail(
                f"could not parse OS_ABI_VERSION_MAJOR from header {header_path}"
            )
        require_abi_major = parsed

    schema_path = (
        Path(args.schema).resolve()
        if args.schema
        else (root / "manifests" / "schema" / "v0.json").resolve()
    )

    if not schema_path.is_file():
        return _fail(f"schema not found at {schema_path}")

    try:
        import jsonschema  # noqa: F401
        from jsonschema import Draft202012Validator
    except ImportError:
        return _fail(
            "python `jsonschema` package is required. "
            "Install via `pip install jsonschema` or `apt-get install "
            "python3-jsonschema`."
        )

    try:
        schema = _load_json(schema_path)
    except json.JSONDecodeError as exc:
        return _fail(f"schema {schema_path} is not valid JSON: {exc}")

    try:
        # Draft202012Validator.check_schema raises on bad meta-schema.
        Draft202012Validator.check_schema(schema)
    except Exception as exc:  # pragma: no cover — defensive
        return _fail(f"schema {schema_path} failed meta-schema check: {exc}")

    validator = Draft202012Validator(schema)

    globs = args.globs or [str(root / "manifests" / "examples" / "*.json")]

    matched: list[Path] = []
    seen: set[Path] = set()
    for pattern in globs:
        # Allow relative patterns to resolve against repo root.
        if not os.path.isabs(pattern):
            pattern = str(root / pattern)
        for hit in sorted(globlib.glob(pattern, recursive=True)):
            p = Path(hit).resolve()
            if p in seen:
                continue
            seen.add(p)
            matched.append(p)

    if not matched:
        return _fail(
            "no manifests matched the supplied globs "
            f"({', '.join(globs)})"
        )

    failures = 0
    for manifest_path in matched:
        rel = manifest_path.relative_to(root) if manifest_path.is_relative_to(root) else manifest_path
        try:
            doc = _load_json(manifest_path)
        except json.JSONDecodeError as exc:
            print(
                f"MANIFEST_VALIDATE:FAIL:{rel}: invalid JSON: {exc}",
                file=sys.stderr,
            )
            failures += 1
            continue

        errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
        manifest_failed = bool(errors)
        for err in errors:
            pointer = _format_pointer(err.absolute_path) or "/"
            print(
                f"MANIFEST_VALIDATE:FAIL:{rel}: at {pointer}: {err.message}",
                file=sys.stderr,
            )

        if require_abi_major is not None:
            # Cross-check os_abi_version against the kernel anchor.
            # Missing / non-int values are already caught by the schema;
            # only emit the ABI mismatch when the field is present and an
            # int so we do not double-report a schema failure.
            abi_val = doc.get("os_abi_version") if isinstance(doc, dict) else None
            if isinstance(abi_val, int) and abi_val != require_abi_major:
                print(
                    f"MANIFEST_VALIDATE:FAIL:{rel}: at /os_abi_version: "
                    f"value {abi_val} does not match required "
                    f"OS_ABI_VERSION_MAJOR={require_abi_major} (issue #227)",
                    file=sys.stderr,
                )
                manifest_failed = True

        if manifest_failed:
            failures += 1
        else:
            print(f"MANIFEST_VALIDATE:PASS:{rel}")

    if failures:
        print(
            f"MANIFEST_VALIDATE:SUMMARY:fail {failures}/{len(matched)}",
            file=sys.stderr,
        )
        return 1

    print(f"MANIFEST_VALIDATE:SUMMARY:pass {len(matched)}/{len(matched)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
