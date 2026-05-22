#!/usr/bin/env python3
# tools/validate_capability_registry.py
#
# Validate docs/abi/capability-registry.json against the canonical sources:
#   - kernel/cap/capability.h        (capability_id_t enum)
#   - build/scripts/test.sh          (test target names)
#   - docs/abi/capability-deny-contract.md §4 (deny-marker grammar)
#   - plans/                         (owning_plan file existence)
#
# Issue: #234. Companion shell + PowerShell wrappers:
#   build/scripts/validate_capability_registry.sh
#   build/scripts/validate_capability_registry.ps1
#
# Emits stable REGISTRY_VALIDATE:* markers to stdout so the lint stage
# can classify failures without parsing English text.
#
# Exit codes:
#   0  every check passed
#   1  one or more checks failed
#   2  environment error (missing input file, malformed JSON, etc.)

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

ENUM_RE = re.compile(r"^\s*(CAP_[A-Z][A-Z0-9_]*)\s*=\s*(\d+)\s*,")
TEST_CASE_RE = re.compile(r"^\s*([a-z_][a-z0-9_]*)\)\s*$")
MARKER_PREFIX = "CAP:DENY:"
CAP_NAME_FIELD_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def emit(line: str) -> None:
    print(line, flush=True)


def parse_enum(header_path: Path) -> dict[str, int]:
    """Parse capability_id_t entries from capability.h.

    Returns dict: cap_id -> numeric_id. Only entries inside the
    capability_id_t enum (CAP_* names) are returned.
    """
    text = header_path.read_text(encoding="utf-8")
    # Pull just the capability_id_t enum body to avoid grabbing
    # CAP_AUDIT_* / CAP_ACCESS_* constants from other enums.
    m = re.search(
        r"typedef\s+enum\s*\{(?P<body>.*?)\}\s*capability_id_t\s*;",
        text,
        re.DOTALL,
    )
    if not m:
        emit("REGISTRY_VALIDATE:FAIL:enum_not_found:capability_id_t")
        sys.exit(2)
    body = m.group("body")
    out: dict[str, int] = {}
    for line in body.splitlines():
        em = ENUM_RE.match(line)
        if em:
            out[em.group(1)] = int(em.group(2))
    if not out:
        emit("REGISTRY_VALIDATE:FAIL:enum_empty:capability_id_t")
        sys.exit(2)
    return out


def parse_test_targets(test_sh_path: Path) -> set[str]:
    """Pull the case-arm labels from build/scripts/test.sh."""
    targets: set[str] = set()
    in_case = False
    text = test_sh_path.read_text(encoding="utf-8")
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith('case "$TEST_NAME"'):
            in_case = True
            continue
        if not in_case:
            continue
        if stripped == "esac":
            break
        # Match `<name>)` arms but skip the catch-all `*)` and `usage)`.
        m = TEST_CASE_RE.match(line)
        if m:
            name = m.group(1)
            if name == "usage":
                continue
            targets.add(name)
    return targets


def parse_registry(registry_path: Path) -> list[dict]:
    try:
        data = json.loads(registry_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        emit(f"REGISTRY_VALIDATE:FAIL:registry_malformed:{exc.msg}")
        sys.exit(2)
    caps = data.get("capabilities")
    if not isinstance(caps, list):
        emit("REGISTRY_VALIDATE:FAIL:registry_malformed:capabilities_missing")
        sys.exit(2)
    return caps


def validate_deny_marker(cap_id: str, marker: str) -> str | None:
    """Return None on success, else a short failure reason."""
    if not marker.startswith(MARKER_PREFIX):
        return "missing_prefix"
    rest = marker[len(MARKER_PREFIX):]
    # Expected shape: <actor>:<cap_name>:<resource>
    parts = rest.split(":", 2)
    if len(parts) != 3:
        return "field_count"
    actor, cap_name, resource = parts
    if not actor:
        return "actor_empty"
    if not CAP_NAME_FIELD_RE.match(cap_name):
        return "cap_name_shape"
    expected = cap_id[len("CAP_"):].lower()
    if cap_name != expected:
        return f"cap_name_mismatch:{cap_name}!={expected}"
    if not resource:
        return "resource_empty"
    if "\n" in resource:
        return "resource_newline"
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate the capability registry.")
    ap.add_argument("--root", default=None,
                    help="Repo root (defaults to git toplevel of this script).")
    ap.add_argument("--registry", default=None,
                    help="Override path to capability-registry.json.")
    ap.add_argument("--header", default=None,
                    help="Override path to kernel/cap/capability.h.")
    ap.add_argument("--test-sh", default=None,
                    help="Override path to build/scripts/test.sh.")
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    root = Path(args.root).resolve() if args.root else here.parent
    registry_path = Path(args.registry) if args.registry else root / "docs" / "abi" / "capability-registry.json"
    header_path = Path(args.header) if args.header else root / "kernel" / "cap" / "capability.h"
    test_sh_path = Path(args.test_sh) if args.test_sh else root / "build" / "scripts" / "test.sh"
    plans_dir = root / "plans"

    emit("REGISTRY_VALIDATE:START")

    for label, p in (("header", header_path), ("registry", registry_path), ("test_sh", test_sh_path)):
        if not p.is_file():
            emit(f"REGISTRY_VALIDATE:FAIL:missing_input:{label}:{p}")
            return 2

    enum_map = parse_enum(header_path)
    targets = parse_test_targets(test_sh_path)
    rows = parse_registry(registry_path)

    failures = 0

    # 1+2+3: bijection between enum and registry, plus numeric_id match.
    seen_in_registry: dict[str, int] = {}
    for row in rows:
        cap_id = row.get("cap_id")
        if not isinstance(cap_id, str):
            emit("REGISTRY_VALIDATE:FAIL:row_missing_cap_id")
            failures += 1
            continue
        if cap_id in seen_in_registry:
            emit(f"REGISTRY_VALIDATE:FAIL:duplicate_registry_entry:{cap_id}")
            failures += 1
            continue
        seen_in_registry[cap_id] = row.get("numeric_id", -1)
        if cap_id not in enum_map:
            emit(f"REGISTRY_VALIDATE:FAIL:registry_not_in_enum:{cap_id}")
            failures += 1
            continue
        if row.get("numeric_id") != enum_map[cap_id]:
            emit(
                "REGISTRY_VALIDATE:FAIL:numeric_id_mismatch:"
                f"{cap_id}:registry={row.get('numeric_id')}:enum={enum_map[cap_id]}"
            )
            failures += 1

    for cap_id in enum_map:
        if cap_id not in seen_in_registry:
            emit(f"REGISTRY_VALIDATE:FAIL:enum_not_in_registry:{cap_id}")
            failures += 1

    if failures == 0:
        emit("REGISTRY_VALIDATE:PASS:enum_registry_bijection")

    # 4: every non-null *_test_target appears in test.sh.
    target_failures = 0
    for row in rows:
        cap_id = row.get("cap_id", "<unknown>")
        for field in ("allow_test_target", "deny_test_target"):
            t = row.get(field)
            if t is None:
                continue
            if not isinstance(t, str):
                emit(f"REGISTRY_VALIDATE:FAIL:test_target_not_string:{cap_id}:{field}")
                target_failures += 1
                continue
            if t not in targets:
                emit(f"REGISTRY_VALIDATE:FAIL:test_target_unknown:{cap_id}:{field}:{t}")
                target_failures += 1
    failures += target_failures
    if target_failures == 0:
        emit("REGISTRY_VALIDATE:PASS:test_targets_resolved")

    # 5: deny_marker grammar.
    marker_failures = 0
    for row in rows:
        cap_id = row.get("cap_id", "<unknown>")
        marker = row.get("deny_marker")
        if not isinstance(marker, str):
            emit(f"REGISTRY_VALIDATE:FAIL:deny_marker_not_string:{cap_id}")
            marker_failures += 1
            continue
        # Resource may be a placeholder like "<path>" — substitute for grammar
        # check so the §4 shape is what we validate, not the placeholder text.
        normalized = re.sub(r"<actor_subject_id>", "0", marker)
        normalized = re.sub(r"<[a-z_]+>", "x", normalized)
        # Ensure no remaining angle brackets sneak through.
        if "<" in normalized or ">" in normalized:
            emit(f"REGISTRY_VALIDATE:FAIL:deny_marker_placeholder_residue:{cap_id}:{marker}")
            marker_failures += 1
            continue
        reason = validate_deny_marker(cap_id, normalized)
        if reason is not None:
            emit(f"REGISTRY_VALIDATE:FAIL:deny_marker_shape:{cap_id}:{reason}")
            marker_failures += 1
    failures += marker_failures
    if marker_failures == 0:
        emit("REGISTRY_VALIDATE:PASS:deny_marker_shape")

    # 6: owning_plan existence.
    plan_failures = 0
    for row in rows:
        cap_id = row.get("cap_id", "<unknown>")
        plan = row.get("owning_plan")
        if plan is None:
            continue
        if not isinstance(plan, str):
            emit(f"REGISTRY_VALIDATE:FAIL:owning_plan_not_string:{cap_id}")
            plan_failures += 1
            continue
        # Plans are recorded as repo-relative paths.
        plan_path = root / plan
        if not plan_path.is_file():
            emit(f"REGISTRY_VALIDATE:FAIL:owning_plan_missing:{cap_id}:{plan}")
            plan_failures += 1
    failures += plan_failures
    if plan_failures == 0:
        emit("REGISTRY_VALIDATE:PASS:owning_plan_resolves")

    # Sanity: ensure plans_dir exists at all (informational).
    if not plans_dir.is_dir():
        emit("REGISTRY_VALIDATE:FAIL:plans_dir_missing")
        failures += 1

    emit("REGISTRY_VALIDATE:DONE")
    return 1 if failures > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
