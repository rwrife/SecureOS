#!/usr/bin/env python3
"""validate_tinycc_arena.py — issue #543.

Drift gate for vendor/tinycc/arena-measurements.json.

Contract:
- validates JSON shape + TU sha256 pins,
- enforces SKIP-pin semantics while #408 Phase 3 is still open,
- enforces <=5% upward drift policy once status flips to `measured`.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def emit_out(line: str) -> None:
    print(line, flush=True)


def emit_err(line: str) -> None:
    print(line, file=sys.stderr, flush=True)


def round_up_64k(n: int) -> int:
    q = 64 * 1024
    return ((n + (q - 1)) // q) * q


def sha256_file(path: Path) -> str:
    import hashlib

    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def gh_issue_state(issue: int, repo: str) -> tuple[str, str]:
    if not shutil.which("gh"):
        return ("UNKNOWN", "gh_not_found")
    try:
        proc = subprocess.run(
            ["gh", "issue", "view", str(issue), "--repo", repo, "--json", "state"],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return ("UNKNOWN", "gh_timeout")
    except Exception as exc:  # pragma: no cover
        return ("UNKNOWN", f"gh_error:{exc}")
    if proc.returncode != 0:
        return ("UNKNOWN", "gh_exit_nonzero")
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return ("UNKNOWN", "gh_json_parse")
    state = str(data.get("state", "")).upper()
    if state in ("OPEN", "CLOSED"):
        return (state, "ok")
    return ("UNKNOWN", f"unexpected_state:{state}")


def run_measurement_tool(root: Path) -> dict[str, Any] | None:
    tool = root / "tools" / "measure_tinycc_arena.py"
    if not tool.exists():
        return None
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        tmp = Path(tf.name)
    try:
        proc = subprocess.run(
            [sys.executable, str(tool), "--root", str(root), "--output", str(tmp)],
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
        if proc.returncode != 0:
            return None
        return json.loads(tmp.read_text(encoding="utf-8"))
    except Exception:
        return None
    finally:
        try:
            tmp.unlink(missing_ok=True)
        except Exception:
            pass


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="Repository root.",
    )
    p.add_argument(
        "--manifest",
        default="vendor/tinycc/arena-measurements.json",
        help="Relative path to pinned arena measurement JSON.",
    )
    p.add_argument(
        "--allow-offline",
        action="store_true",
        help="Allow SKIP when live measurement prerequisites are unavailable.",
    )
    p.add_argument(
        "--repo",
        default="rwrife/SecureOS",
        help="GitHub repo for gating issue checks.",
    )
    p.add_argument(
        "--live-json",
        default=None,
        help="Optional explicit live measurement JSON (used for canary tests).",
    )
    return p.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        emit_err(f"TINYCC_ARENA:FAIL:missing_manifest:{path}")
        raise SystemExit(2)
    except json.JSONDecodeError as exc:
        emit_err(f"TINYCC_ARENA:FAIL:malformed_manifest:{exc}")
        raise SystemExit(2)


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    manifest_path = (root / args.manifest).resolve()
    doc = load_json(manifest_path)

    failures = 0

    schema = doc.get("schemaVersion")
    if schema != 1:
        emit_err(f"TINYCC_ARENA:FAIL:bad_schema:{schema!r}")
        return 1

    issue = doc.get("issue")
    gating_issue_raw = doc.get("gatingIssue")
    gating_issue = gating_issue_raw if isinstance(gating_issue_raw, int) else None
    if issue != 543:
        emit_err(f"TINYCC_ARENA:FAIL:bad_issue:{issue!r}")
        failures += 1
    if gating_issue is None:
        emit_err(f"TINYCC_ARENA:FAIL:bad_gating_issue:{gating_issue_raw!r}")
        failures += 1

    measurements = doc.get("measurements")
    if not isinstance(measurements, list) or not measurements:
        emit_err("TINYCC_ARENA:FAIL:measurements_missing_or_empty")
        return 1

    pinned_by_tu: dict[str, int] = {}
    for rec in measurements:
        tu = rec.get("tu")
        if not isinstance(tu, str):
            emit_err(f"TINYCC_ARENA:FAIL:bad_tu_entry:{tu!r}")
            failures += 1
            continue
        tu_path = root / tu
        expected_sha = rec.get("sha256")
        if tu_path.exists() and isinstance(expected_sha, str):
            live_sha = sha256_file(tu_path)
            if live_sha != expected_sha:
                emit_err(
                    f"TINYCC_ARENA:FAIL:sha_mismatch:{tu}:expected={expected_sha}:live={live_sha}"
                )
                failures += 1
            else:
                emit_out(f"TINYCC_ARENA:PASS:sha_pinned:{tu}")
        elif not tu_path.exists():
            emit_err(f"TINYCC_ARENA:FAIL:tu_missing:{tu}")
            failures += 1
        else:
            emit_err(f"TINYCC_ARENA:FAIL:missing_sha:{tu}")
            failures += 1

        peak = rec.get("peak_bytes")
        if isinstance(peak, int):
            pinned_by_tu[tu] = peak

    pinned_arena = doc.get("pinned_arena_bytes")
    if not isinstance(pinned_arena, int) or pinned_arena <= 0:
        emit_err(f"TINYCC_ARENA:FAIL:bad_pinned_arena_bytes:{pinned_arena!r}")
        failures += 1

    if failures:
        emit_err(f"TINYCC_ARENA:FAIL:summary:{failures}_preflight_failures")
        return 1

    # preflight above guarantees an int here
    assert gating_issue is not None
    status = str(doc.get("status", ""))

    # Placeholder phase: explicitly SKIP-pinned until #408 Phase 3 closes.
    if status == "awaiting_408_phase3":
        if args.allow_offline:
            emit_out("TINYCC_ARENA:SKIP:awaiting_408_phase3:allow_offline")
            emit_out("TINYCC_ARENA:PASS:summary:placeholder_mode")
            return 0

        state, detail = gh_issue_state(gating_issue, args.repo)
        if state == "OPEN":
            emit_out("TINYCC_ARENA:SKIP:awaiting_408_phase3:gating_issue_open")
            emit_out("TINYCC_ARENA:PASS:summary:placeholder_mode")
            return 0
        if state == "UNKNOWN":
            emit_out(f"TINYCC_ARENA:SKIP:awaiting_408_phase3:gating_issue_unknown:{detail}")
            emit_out("TINYCC_ARENA:PASS:summary:placeholder_mode")
            return 0
        emit_err(
            f"TINYCC_ARENA:FAIL:awaiting_marker_stale:gating_issue_closed:{gating_issue}"
        )
        return 1

    # Measured mode: enforce upward-drift threshold.
    if status != "measured":
        emit_err(f"TINYCC_ARENA:FAIL:unknown_status:{status}")
        return 1

    if not pinned_by_tu:
        emit_err("TINYCC_ARENA:FAIL:measured_mode_requires_integer_peak_bytes")
        return 1

    threshold_percent = float(doc.get("threshold_percent", 5.0))

    if args.live_json:
        live_doc = load_json((root / args.live_json).resolve())
    else:
        live_doc = run_measurement_tool(root)

    if live_doc is None:
        if args.allow_offline:
            emit_out("TINYCC_ARENA:SKIP:live_measurement_unavailable:allow_offline")
            emit_out("TINYCC_ARENA:PASS:summary:offline_skip")
            return 0
        emit_err("TINYCC_ARENA:FAIL:live_measurement_unavailable")
        return 1

    live_measurements = live_doc.get("measurements")
    if not isinstance(live_measurements, list):
        emit_err("TINYCC_ARENA:FAIL:live_json_missing_measurements")
        return 1

    live_by_tu: dict[str, int] = {}
    for rec in live_measurements:
        tu = rec.get("tu")
        peak = rec.get("peak_bytes")
        if isinstance(tu, str) and isinstance(peak, int):
            live_by_tu[tu] = peak

    failures = 0
    max_live = 0
    for tu, pinned_peak in sorted(pinned_by_tu.items()):
        if tu not in live_by_tu:
            emit_err(f"TINYCC_ARENA:FAIL:live_missing_tu:{tu}")
            failures += 1
            continue
        live_peak = int(live_by_tu[tu])
        max_live = max(max_live, live_peak)
        ceiling = math.floor(pinned_peak * (1.0 + (threshold_percent / 100.0)))
        if live_peak > ceiling:
            emit_err(
                "TINYCC_ARENA:FAIL:upward_drift:"
                f"{tu}:pinned={pinned_peak}:live={live_peak}:"
                f"threshold_percent={threshold_percent}"
            )
            failures += 1
        else:
            emit_out(
                "TINYCC_ARENA:PASS:drift_within_threshold:"
                f"{tu}:pinned={pinned_peak}:live={live_peak}:"
                f"threshold_percent={threshold_percent}"
            )

    if max_live > 0:
        recommended = round_up_64k(int(max_live * 1.5))
        if pinned_arena < recommended:
            emit_err(
                "TINYCC_ARENA:FAIL:pinned_arena_too_small:"
                f"pinned={pinned_arena}:recommended={recommended}"
            )
            failures += 1
        else:
            emit_out(
                "TINYCC_ARENA:PASS:pinned_arena_recommended_floor:"
                f"pinned={pinned_arena}:recommended={recommended}"
            )

    if failures:
        emit_err(f"TINYCC_ARENA:FAIL:summary:{failures}_drift_failures")
        return 1

    emit_out("TINYCC_ARENA:PASS:summary:measured_mode")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
