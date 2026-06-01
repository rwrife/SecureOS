#!/usr/bin/env python3
"""validate_m7_markers.py — issue #494 (M7 markers drift gate)

Pin `tests/m7_toolchain/markers.json` (scaffolded by #423, umbrella
#403) against the surfaces that consume it so a future slice cannot
silently rename a marker, drop a TEST_TARGETS arm, or leave an
`awaiting_<n>` reason pointing at an already-closed gating issue.

This validator follows the same shape as the registry / abi-stamp /
sosh-contract validators (#234 / #297 / #351): pure stdlib, stable
machine-readable markers, no markdown parser dependency. Wired by:

  * build/scripts/validate_m7_markers.sh  (parity wrapper)
  * build/scripts/test.sh validate_m7_markers
  * tests/harness/m7_markers_drift_test.sh  (negative canary)

What it checks (per markers.json schemaVersion 1):

  1. Every marker `name` in `markers.json` appears as a `toolchain_*)`
     case arm in `build/scripts/test.sh`, AND every `toolchain_*` arm
     in `test.sh` appears in `markers.json` (bidirectional bijection).
  2. Every marker `name` appears in the `TEST_TARGETS` array of
     `build/scripts/validate_bundle.sh`. Same orphan-from-
     TEST_TARGETS shape catalogued by #129 / #366 / #401 / #414 /
     #469 / #482 / #487.
  3. For every marker, `tests/m7_toolchain/<name>.sh` exists and
     contains a literal `TEST:PASS:<name>` line (the canonical
     marker emission the bundle gate scrapes).
  4. For every marker, the same per-marker script contains a literal
     `TEST:SKIP:<name>:<reason>` line whose `<reason>` matches the
     marker's `reason` field in markers.json (catches the drift mode
     where the json gets retargeted but the script still SKIPs with
     the old `awaiting_<n>` reason — exactly the #456 retarget shape
     this gate was filed for).
  5. For every `gatingIssue`, when network/gh is available, the
     issue is `OPEN` AND the marker's `reason` field still spells
     `awaiting_<gatingIssue>` (catches a json edit that updates
     `gatingIssue` but forgets the paired `reason`, and vice versa).
     Use `--allow-offline` to skip the live gh lookup and emit a
     `M7_MARKER:SKIP:gh_unavailable:<reason>` line instead — this is
     what CI uses on hosts without `gh` (e.g. sandbox-only laps).

Output contract (mirrors the other validators so the existing test-
marker scrapers / #234 grep harness can classify failures without
parsing English text):

  * On per-check pass: `M7_MARKER:PASS:<check>:<name>`
  * On overall pass:   `M7_MARKER:PASS:overall`
  * On any failure:    `M7_MARKER:FAIL:<reason>:<name>[:<detail>]`
  * On skipped gh lookup: `M7_MARKER:SKIP:gh_unavailable:<reason>`

Exit codes:
  0  every check passed
  1  one or more `M7_MARKER:FAIL:*` markers emitted
  2  environment / usage error (missing inputs, malformed JSON,
     no markers.json on disk, etc.)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# `toolchain_*` only — the M7 markers all share that prefix per the
# plan's "Acceptance tests" section.
MARKER_NAME_RE = re.compile(r"\btoolchain_[a-z0-9_]+\b")
# A `case` arm in bash that names one or more targets, possibly
# alternated with `|`, terminated by `)`. We only consume arms that
# include at least one `toolchain_*` word so we don't snag unrelated
# arms in test.sh.
CASE_ARM_RE = re.compile(r"^\s*([a-zA-Z0-9_|]+)\)\s*$")
TEST_PASS_RE_TMPL = "TEST:PASS:{name}"
TEST_SKIP_RE_TMPL = re.compile(r"TEST:SKIP:([a-z][a-z0-9_]*):([A-Za-z0-9_]+)")


def emit(line: str) -> None:
    print(line, flush=True)


def err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def load_markers(path: Path) -> Tuple[dict, List[dict]]:
    try:
        with path.open("r", encoding="utf-8") as fh:
            doc = json.load(fh)
    except FileNotFoundError:
        err(f"M7_MARKER:FAIL:markers_json_missing:{path}")
        sys.exit(2)
    except json.JSONDecodeError as exc:
        err(f"M7_MARKER:FAIL:markers_json_malformed:{path}:{exc}")
        sys.exit(2)
    markers = doc.get("markers")
    if not isinstance(markers, list) or not markers:
        err(f"M7_MARKER:FAIL:markers_json_no_markers:{path}")
        sys.exit(2)
    return doc, markers


def parse_test_sh_arms(test_sh: Path) -> Set[str]:
    """Return the set of `toolchain_*` case-arm names in test.sh."""
    try:
        text = test_sh.read_text(encoding="utf-8")
    except FileNotFoundError:
        err(f"M7_MARKER:FAIL:test_sh_missing:{test_sh}")
        sys.exit(2)
    found: Set[str] = set()
    for line in text.splitlines():
        m = CASE_ARM_RE.match(line)
        if not m:
            continue
        arm = m.group(1)
        if "toolchain_" not in arm:
            continue
        for name in arm.split("|"):
            name = name.strip()
            if name.startswith("toolchain_"):
                found.add(name)
    return found


def parse_test_targets(bundle_sh: Path) -> Set[str]:
    """Return every `toolchain_*` name appearing inside the
    `TEST_TARGETS=( ... )` array in validate_bundle.sh."""
    try:
        text = bundle_sh.read_text(encoding="utf-8")
    except FileNotFoundError:
        err(f"M7_MARKER:FAIL:validate_bundle_sh_missing:{bundle_sh}")
        sys.exit(2)
    # Slice from `TEST_TARGETS=(` to the matching closing `)` line.
    # validate_bundle.sh has exactly one TEST_TARGETS array and the
    # closing paren sits alone on a line (the same shape every other
    # validator parses; see validate_capability_registry.py).
    start = text.find("TEST_TARGETS=(")
    if start < 0:
        err(f"M7_MARKER:FAIL:test_targets_array_missing:{bundle_sh}")
        sys.exit(2)
    rest = text[start:]
    end = rest.find("\n)")
    if end < 0:
        err(f"M7_MARKER:FAIL:test_targets_array_unterminated:{bundle_sh}")
        sys.exit(2)
    block = rest[:end]
    found: Set[str] = set()
    for raw in block.splitlines():
        # Strip comments and whitespace; tokens are one-per-line in
        # validate_bundle.sh today, but be defensive.
        line = raw.split("#", 1)[0].strip()
        for tok in line.split():
            if tok.startswith("toolchain_"):
                found.add(tok)
    return found


def parse_script_markers(script: Path, name: str) -> Tuple[bool, Optional[str]]:
    """Return (has_pass_line, skip_reason_or_None) for a per-marker stub.

    `has_pass_line` is True iff the script contains the literal
    `TEST:PASS:<name>`. `skip_reason` is the `<reason>` segment of
    the first `TEST:SKIP:<name>:<reason>` line in the script, or
    None if the script does not emit a SKIP line for `name`.
    """
    try:
        text = script.read_text(encoding="utf-8")
    except FileNotFoundError:
        return (False, None)
    has_pass = (TEST_PASS_RE_TMPL.format(name=name)) in text
    skip_reason: Optional[str] = None
    for m in TEST_SKIP_RE_TMPL.finditer(text):
        if m.group(1) == name:
            skip_reason = m.group(2)
            break
    return (has_pass, skip_reason)


def gh_issue_state(num: int) -> Optional[str]:
    """Return 'OPEN' / 'CLOSED' / None on error. Network call."""
    try:
        proc = subprocess.run(
            ["gh", "issue", "view", str(num),
             "--repo", "rwrife/SecureOS",
             "--json", "state", "-q", ".state"],
            check=False, capture_output=True, text=True, timeout=15,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if proc.returncode != 0:
        return None
    state = proc.stdout.strip().upper()
    return state or None


def check(root: Path, allow_offline: bool) -> int:
    markers_json = root / "tests" / "m7_toolchain" / "markers.json"
    test_sh = root / "build" / "scripts" / "test.sh"
    bundle_sh = root / "build" / "scripts" / "validate_bundle.sh"
    scripts_dir = root / "tests" / "m7_toolchain"

    doc, markers = load_markers(markers_json)
    json_names: Set[str] = set()
    for entry in markers:
        name = entry.get("name")
        if not isinstance(name, str) or not name.startswith("toolchain_"):
            err(f"M7_MARKER:FAIL:bad_marker_name:{name!r}")
            return 1
        if name in json_names:
            err(f"M7_MARKER:FAIL:duplicate_marker_name:{name}")
            return 1
        json_names.add(name)

    test_sh_arms = parse_test_sh_arms(test_sh)
    test_targets = parse_test_targets(bundle_sh)

    failures = 0

    # (1) bijection json ↔ test.sh case arms
    for name in sorted(json_names):
        if name in test_sh_arms:
            emit(f"M7_MARKER:PASS:test_sh_arm:{name}")
        else:
            err(f"M7_MARKER:FAIL:missing_test_sh_arm:{name}")
            failures += 1
    for name in sorted(test_sh_arms - json_names):
        err(f"M7_MARKER:FAIL:orphan_test_sh_arm:{name}")
        failures += 1

    # (2) every json name is in TEST_TARGETS
    for name in sorted(json_names):
        if name in test_targets:
            emit(f"M7_MARKER:PASS:test_targets:{name}")
        else:
            err(f"M7_MARKER:FAIL:missing_test_targets:{name}")
            failures += 1
    for name in sorted(test_targets - json_names):
        # An orphan toolchain_* in TEST_TARGETS not in json is the
        # other half of the drift; surface it but don't fail because
        # `in_os_toolchain_dev_dir` exists in TEST_TARGETS and is NOT
        # an M7 marker — guard with prefix check above (already done).
        err(f"M7_MARKER:FAIL:orphan_test_targets:{name}")
        failures += 1

    # (3) + (4) per-script PASS/SKIP literals match json reason
    for entry in markers:
        name = entry["name"]
        reason = entry.get("reason")
        script = scripts_dir / f"{name}.sh"
        has_pass, skip_reason = parse_script_markers(script, name)
        if not script.exists():
            err(f"M7_MARKER:FAIL:script_missing:{name}:{script}")
            failures += 1
            continue
        if not has_pass:
            err(f"M7_MARKER:FAIL:pass_line_missing:{name}")
            failures += 1
        else:
            emit(f"M7_MARKER:PASS:script_pass_line:{name}")
        if not isinstance(reason, str):
            err(f"M7_MARKER:FAIL:json_reason_missing:{name}")
            failures += 1
        elif skip_reason is None:
            err(f"M7_MARKER:FAIL:skip_line_missing:{name}")
            failures += 1
        elif skip_reason != reason:
            err(
                f"M7_MARKER:FAIL:skip_reason_mismatch:{name}:"
                f"script={skip_reason}:json={reason}")
            failures += 1
        else:
            emit(f"M7_MARKER:PASS:script_skip_reason:{name}")

    # (5) gating-issue state ↔ reason
    for entry in markers:
        name = entry["name"]
        gating = entry.get("gatingIssue")
        reason = entry.get("reason")
        if not isinstance(gating, int):
            err(f"M7_MARKER:FAIL:gating_issue_missing:{name}")
            failures += 1
            continue
        expected_reason = f"awaiting_{gating}"
        if reason != expected_reason:
            err(
                f"M7_MARKER:FAIL:reason_issue_mismatch:{name}:"
                f"reason={reason}:expected={expected_reason}")
            failures += 1
            continue
        if allow_offline:
            emit(f"M7_MARKER:SKIP:gh_unavailable:{name}")
            continue
        state = gh_issue_state(gating)
        if state is None:
            emit(f"M7_MARKER:SKIP:gh_unavailable:{name}")
            continue
        if state == "OPEN":
            emit(f"M7_MARKER:PASS:gating_issue_open:{name}")
        else:
            err(
                f"M7_MARKER:FAIL:gating_issue_closed:{name}:"
                f"issue=#{gating}:state={state}")
            failures += 1

    if failures:
        return 1
    emit("M7_MARKER:PASS:overall")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", default=".",
        help="Repository root (defaults to current directory).")
    parser.add_argument(
        "--allow-offline", action="store_true",
        help="Skip the live `gh issue view` lookup; emit "
             "M7_MARKER:SKIP:gh_unavailable:<name> for each gating "
             "issue instead. Used on CI laps without network/gh.")
    args = parser.parse_args()
    return check(Path(args.root).resolve(), args.allow_offline)


if __name__ == "__main__":
    sys.exit(main())
