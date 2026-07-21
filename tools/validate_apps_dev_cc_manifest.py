#!/usr/bin/env python3
"""Host drift gate for the canonical /apps/dev/cc manifest (issue #573).

Compares:
  - pinned canonical manifest: manifests/apps_dev_cc.json
  - staged app manifest (from #540): user/apps/cc/manifest.json

Behavior:
  * While #540 is still open (staged manifest path absent), emit a deterministic
    SKIP marker and exit 0.
  * Once staged manifest exists, fail on any byte-level JSON-shape divergence
    after canonical normalization (sorted keys, compact separators).
  * Always enforce the issue-573 invariants on the pinned canonical manifest:
      - owner.kind == "internal"
      - capabilities.request includes CAP_FS_READ + CAP_FS_WRITE
      - capabilities.request excludes CAP_APP_EXEC / CAP_NETWORK
      - runtime.arena_bytes exists and is in [65536, 16777216]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

PINNED_REL = Path("manifests/apps_dev_cc.json")
STAGED_REL = Path("user/apps/cc/manifest.json")

REQ_CAPS = {"CAP_FS_READ", "CAP_FS_WRITE"}
FORBIDDEN_CAPS = {"CAP_APP_EXEC", "CAP_NETWORK"}
ARENA_MIN = 65536
ARENA_MAX = 16777216


def _emit(msg: str) -> None:
    print(msg)


def _load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"top-level value is not an object: {path}")
    return data


def _norm(doc: dict) -> str:
    return json.dumps(doc, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _fail(reason: str) -> int:
    _emit(f"CC_MANIFEST:FAIL:{reason}")
    return 1


def _check_pinned_shape(pinned: dict) -> int:
    owner_kind = (((pinned.get("owner") or {}).get("kind")))
    if owner_kind != "internal":
        return _fail(f"owner_kind_not_internal:{owner_kind}")

    caps = (pinned.get("capabilities") or {}).get("request")
    if not isinstance(caps, list):
        return _fail("capabilities_request_missing_or_not_array")
    cap_set = set(str(x) for x in caps)

    missing = sorted(REQ_CAPS - cap_set)
    if missing:
        return _fail(f"missing_required_caps:{','.join(missing)}")

    forbidden_present = sorted(FORBIDDEN_CAPS & cap_set)
    if forbidden_present:
        return _fail(f"forbidden_caps_present:{','.join(forbidden_present)}")

    runtime = pinned.get("runtime")
    if not isinstance(runtime, dict):
        return _fail("runtime_object_missing")
    arena = runtime.get("arena_bytes")
    if not isinstance(arena, int):
        return _fail("runtime_arena_bytes_missing_or_not_int")
    if arena < ARENA_MIN or arena > ARENA_MAX:
        return _fail(f"runtime_arena_bytes_out_of_range:{arena}")

    _emit("CC_MANIFEST:PASS:pinned_shape")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        default=None,
        help="Repository root (defaults to two levels above this script).",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[1]
    pinned_path = root / PINNED_REL
    staged_path = root / STAGED_REL

    if not pinned_path.is_file():
        return _fail(f"missing_pinned_manifest:{PINNED_REL.as_posix()}")

    try:
        pinned = _load_json(pinned_path)
    except Exception as exc:
        _emit(f"CC_MANIFEST:FAIL:pinned_manifest_parse_error:{exc}")
        return 1

    rc = _check_pinned_shape(pinned)
    if rc != 0:
        return rc

    # #540 not landed yet: staged app manifest path does not exist.
    if not staged_path.is_file():
        _emit("CC_MANIFEST:SKIP:apps_dev_cc:awaiting_540")
        return 0

    try:
        staged = _load_json(staged_path)
    except Exception as exc:
        _emit(f"CC_MANIFEST:FAIL:staged_manifest_parse_error:{exc}")
        return 1

    if _norm(pinned) != _norm(staged):
        _emit("CC_MANIFEST:FAIL:pinned_vs_staged_drift")
        return 1

    _emit("CC_MANIFEST:PASS:pinned_vs_staged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
