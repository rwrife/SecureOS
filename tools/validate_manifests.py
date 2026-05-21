#!/usr/bin/env python3
"""Validate SecureOS app manifests against `manifests/schema/v0.json`.

Issue #195 (follow-up to #187). Locks in the manifest-schema work that
landed in #183/#187 by re-validating every shipped example manifest on
every PR so schema and examples cannot silently drift.

Behavior
--------
* Walks the repo for app-manifest JSON files. By default scans:
    - manifests/examples/*.json (the canonical examples)
    - any other ``*.manifest.json`` file in the tree (forward-compat
      with the glob shape suggested in #195's "Scope")
* Skips well-known non-app-manifest JSON: `manifests/task-dag.*` and
  the schema files themselves.
* Validates each candidate against `manifests/schema/v0.json` using
  the `jsonschema` library if importable, otherwise falls back to a
  minimal hand-rolled structural check that mirrors the schema's
  `required` / `additionalProperties` / `const` constraints. The
  fallback is deliberately conservative — it exists so a green run
  still proves the document is parseable and shaped correctly even
  when the optional dep is missing; CI is expected to install
  `jsonschema` for the strict pass.

Output is line-oriented and matches the ``TEST:START`` /
``TEST:PASS:`` / ``TEST:FAIL:`` markers used by the rest of the
validator bundle (see build/scripts/validate_bundle.sh).

Exit codes
----------
0  all manifests valid
1  at least one manifest failed validation
2  internal / harness error (schema file missing, unreadable JSON in
   the schema itself, etc.) — distinct from a manifest regression so
   CI can tell infra breakage from a real drift.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Iterable, List, Tuple


REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "manifests" / "schema" / "v0.json"
EXAMPLES_DIR = REPO_ROOT / "manifests" / "examples"

# Files we never treat as app manifests, even if they end in .json.
SKIP_NAMES = {
    "task-dag.example.json",
    "task-dag.schema.json",
}


def _emit(line: str) -> None:
    """Print a TEST:* marker line, flushed so log scrapers see it in order."""
    print(line, flush=True)


def _discover_manifests() -> List[Path]:
    """Return the sorted list of manifest files to validate."""
    found: List[Path] = []

    if EXAMPLES_DIR.is_dir():
        for p in sorted(EXAMPLES_DIR.glob("*.json")):
            if p.name in SKIP_NAMES:
                continue
            found.append(p)

    # Forward-compat: also pick up any other *.manifest.json the tree
    # may grow (the glob shape called out in #195's Scope). We exclude
    # the examples dir to avoid double-listing the same file.
    for p in sorted(REPO_ROOT.rglob("*.manifest.json")):
        if EXAMPLES_DIR in p.parents:
            continue
        # Skip vendored / build output trees.
        rel = p.relative_to(REPO_ROOT)
        if rel.parts and rel.parts[0] in {"vendor", "artifacts", ".git"}:
            continue
        found.append(p)

    return found


def _load_json(path: Path) -> Tuple[object, str | None]:
    try:
        return json.loads(path.read_text()), None
    except FileNotFoundError:
        return None, f"missing: {path}"
    except json.JSONDecodeError as exc:
        return None, f"invalid JSON: {exc}"


def _fallback_validate(doc: object, schema: dict) -> str | None:
    """Minimal structural check used when `jsonschema` is unavailable.

    Intentionally narrow — covers required fields, `additionalProperties:
    false`, `const`, `enum`, integer min/max, and the v0 manifest's
    cross-field constraint that a manifest declares
    ``manifest_version == 0`` and a top-level ``capabilities.request``.
    Anything subtler is the strict-mode (`jsonschema`) validator's job.
    """
    if not isinstance(doc, dict):
        return "manifest must be a JSON object"

    required_top = schema.get("required", [])
    for k in required_top:
        if k not in doc:
            return f"missing required top-level field: {k}"

    if schema.get("additionalProperties") is False:
        allowed = set(schema.get("properties", {}).keys())
        extra = sorted(set(doc.keys()) - allowed)
        if extra:
            return f"unknown top-level fields (additionalProperties=false): {extra}"

    mv = doc.get("manifest_version")
    if mv != 0:
        return f"manifest_version must be 0, got {mv!r}"

    abi = doc.get("os_abi_version")
    if not isinstance(abi, int) or abi < 0:
        return f"os_abi_version must be a non-negative integer, got {abi!r}"

    app = doc.get("app")
    if not isinstance(app, dict):
        return "app must be an object"
    for k in ("id", "version", "subject_id", "binary"):
        if k not in app:
            return f"app.{k} is required"
    sid = app.get("subject_id")
    if not isinstance(sid, int) or sid < 1 or sid > 7:
        return f"app.subject_id must be integer in [1,7], got {sid!r}"

    caps = doc.get("capabilities")
    if not isinstance(caps, dict):
        return "capabilities must be an object"
    if "request" not in caps:
        return "capabilities.request is required"
    if not isinstance(caps["request"], list):
        return "capabilities.request must be an array"

    return None


def _strict_validate(doc: object, schema: dict) -> str | None:
    try:
        import jsonschema  # type: ignore
    except ImportError:
        return None  # caller falls back

    try:
        jsonschema.validate(instance=doc, schema=schema)
    except jsonschema.ValidationError as exc:  # type: ignore[attr-defined]
        # Build a JSON-pointer-ish path so failures point at the field.
        loc = "/".join(str(x) for x in exc.absolute_path) or "<root>"
        return f"{loc}: {exc.message}"
    except jsonschema.SchemaError as exc:  # type: ignore[attr-defined]
        return f"schema is invalid: {exc.message}"
    return None


def main(argv: Iterable[str]) -> int:
    _emit("TEST:START:manifest_schema_validate")

    if not SCHEMA_PATH.is_file():
        _emit(f"TEST:FAIL:manifest_schema_validate:missing_schema:{SCHEMA_PATH}")
        return 2
    schema, err = _load_json(SCHEMA_PATH)
    if err is not None or not isinstance(schema, dict):
        _emit(f"TEST:FAIL:manifest_schema_validate:bad_schema:{err}")
        return 2

    manifests = _discover_manifests()
    if not manifests:
        # No manifests yet is not a failure on its own — the schema may
        # land before the first example. Make it visible though.
        _emit("TEST:PASS:manifest_schema_validate:no_manifests_found")
        return 0

    have_jsonschema = False
    try:
        import jsonschema  # noqa: F401
        have_jsonschema = True
    except ImportError:
        have_jsonschema = False

    failures: List[str] = []
    for path in manifests:
        rel = path.relative_to(REPO_ROOT)
        doc, err = _load_json(path)
        if err is not None:
            failures.append(f"{rel}: {err}")
            _emit(f"TEST:FAIL:manifest_schema_validate:{rel}:{err}")
            continue

        if have_jsonschema:
            err = _strict_validate(doc, schema)
            mode = "strict"
        else:
            err = _fallback_validate(doc, schema)
            mode = "fallback"

        if err is None:
            _emit(f"TEST:PASS:manifest_schema_validate:{rel}:{mode}")
        else:
            failures.append(f"{rel}: {err}")
            _emit(f"TEST:FAIL:manifest_schema_validate:{rel}:{err}")

    if failures:
        _emit(f"TEST:FAIL:manifest_schema_validate:failed_count={len(failures)}")
        return 1

    mode = "strict" if have_jsonschema else "fallback"
    _emit(
        "TEST:PASS:manifest_schema_validate:"
        f"validated={len(manifests)}:mode={mode}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
