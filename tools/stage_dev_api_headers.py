#!/usr/bin/env python3
"""Stage 8.3-safe guest API/ABI headers for DEMO-04 (#768).

Called by build_disk_image.sh before disk population. Host SDK headers retain their
canonical names; guest samples include osapi.h, which imports staged osabi.h.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "user/include"
TARGET = ROOT / "artifacts/user/include"


def main() -> None:
    api = (SOURCE / "secureos_api.h").read_bytes()
    old = b'#include "secureos_abi.h"'
    if api.count(old) != 1:
        raise ValueError("secureos_api.h must import secureos_abi.h exactly once")
    abi = (SOURCE / "secureos_abi.h").read_bytes()
    if not api or not abi:
        raise ValueError("empty public API/ABI header")
    TARGET.mkdir(parents=True, exist_ok=True)
    (TARGET / "osabi.h").write_bytes(abi)
    (TARGET / "osapi.h").write_bytes(api.replace(old, b'#include "osabi.h"'))


if __name__ == "__main__":
    main()
