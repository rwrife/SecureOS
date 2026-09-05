#!/usr/bin/env python3
"""Pin launcher owner_kind audit-marker contract against manifest fixtures.

Issue: #554

This host gate deliberately validates the contract shape *before* #410 wires the
full runtime emit path in QEMU. It enforces:
  - launch.granted carries owner_kind=<internal|external|local>
  - launch.denied carries owner_kind=<internal|external|local>
  - owner omitted in manifest defaults to internal

The fixture manifests are the same owner-kind examples documented in
`docs/abi/manifest.md` §5.6.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

TARGET = "launcher_owner_kind_audit_marker"

_ALLOWED_KINDS = {"internal", "external", "local"}
_GRANTED_SHAPE = "launch.granted:owner_kind=<owner_kind>"
_DENIED_SHAPE = "launch.denied:owner_kind=<owner_kind>:subject=<sid>:reason=<reason>"
_GRANTED_RE = re.compile(r"^launch\.granted:owner_kind=(internal|external|local)$")
_DENIED_RE = re.compile(
    r"^launch\.denied:owner_kind=(internal|external|local):subject=[1-9][0-9]*:reason=[A-Za-z0-9_.-]+$"
)


def _out(msg: str) -> None:
    print(msg, flush=True)


def _fail(code: str, detail: str) -> int:
    _out(f"TEST:FAIL:{TARGET}:{code}:{detail}")
    return 1


def _load_json(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise RuntimeError(f"missing_fixture:{path}")
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"malformed_json:{path}:{exc}")
    if not isinstance(data, dict):
        raise RuntimeError(f"fixture_not_object:{path}")
    return data


def _resolve_owner_kind(doc: dict) -> tuple[str, bool]:
    owner = doc.get("owner")
    if owner is None:
        return "internal", True
    if not isinstance(owner, dict):
        raise RuntimeError("owner_not_object")
    kind = owner.get("kind")
    if kind is None:
        return "internal", True
    if not isinstance(kind, str):
        raise RuntimeError("owner_kind_not_string")
    norm = kind.strip()
    if norm not in _ALLOWED_KINDS:
        raise RuntimeError(f"owner_kind_unknown:{norm}")
    return norm, False


def _load_launch_shapes(audit_markers_json: Path) -> tuple[str, str]:
    try:
        doc = json.loads(audit_markers_json.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise RuntimeError(f"missing_registry:{audit_markers_json}")
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"malformed_registry:{exc}")

    rows = doc.get("markers")
    if not isinstance(rows, list):
        raise RuntimeError("invalid_registry_markers")

    shape_by_prefix: dict[str, str] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        prefix = row.get("prefix")
        shape = row.get("shape")
        if isinstance(prefix, str) and isinstance(shape, str):
            shape_by_prefix[prefix] = shape

    granted = shape_by_prefix.get("launch.granted")
    denied = shape_by_prefix.get("launch.denied")
    if granted is None or denied is None:
        raise RuntimeError("missing_launch_rows")
    return granted, denied


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate launch owner_kind audit marker contract")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args()

    root = Path(args.root).resolve()
    fixtures = [
        (
            root / "manifests/examples/helloapp.owner_internal.json",
            "internal",
            "internal",
        ),
        (
            root / "manifests/examples/helloapp.owner_external.json",
            "external",
            "external",
        ),
        (
            root / "manifests/examples/helloapp.owner_kind_local.json",
            "local",
            "local",
        ),
        (
            root / "manifests/examples/helloapp.json",
            "internal",
            "default_when_omitted",
        ),
    ]

    try:
        granted_shape, denied_shape = _load_launch_shapes(root / "docs/abi/audit-markers.json")
    except RuntimeError as exc:
        return _fail("registry", str(exc))

    if granted_shape != _GRANTED_SHAPE:
        return _fail("shape_granted", repr(granted_shape))
    if denied_shape != _DENIED_SHAPE:
        return _fail("shape_denied", repr(denied_shape))

    subject = 42
    reason = "policy_denied"

    for fixture_path, expected_kind, submarker in fixtures:
        try:
            manifest = _load_json(fixture_path)
            resolved_kind, owner_omitted = _resolve_owner_kind(manifest)
        except RuntimeError as exc:
            return _fail("fixture", str(exc))

        if resolved_kind != expected_kind:
            return _fail(
                "resolved_kind",
                f"{fixture_path.name}:expected={expected_kind}:actual={resolved_kind}",
            )

        if submarker == "default_when_omitted" and not owner_omitted:
            return _fail("default_case", f"{fixture_path.name}:owner_not_omitted")

        granted = f"launch.granted:owner_kind={resolved_kind}"
        denied = (
            "launch.denied:"
            f"owner_kind={resolved_kind}:subject={subject}:reason={reason}"
        )

        if not _GRANTED_RE.match(granted):
            return _fail("granted_format", granted)
        if not _DENIED_RE.match(denied):
            return _fail("denied_format", denied)

        _out(f"TEST:PASS:{TARGET}:{submarker}")

    _out(f"TEST:PASS:{TARGET}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
