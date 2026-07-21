#!/usr/bin/env python3
"""Drift gate for host-built dev/hello.c SOF golden (issue #619).

Builds the canonical hello sample with the same freestanding flags used by
build/scripts/build_user_app.sh, wraps ELF -> SOF, and validates the output
hash + pinned input-source hashes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MARKER = "HELLO_GOLDEN"
DEFAULT_DATE = "1970-01-01"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list[str], cwd: Path) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")


def ensure_executable(path: Path) -> None:
    mode = path.stat().st_mode
    if not (mode & 0o111):
        path.chmod(mode | 0o755)


def compute_state(root: Path, sof_date: str) -> dict:
    hello_src = root / "dev/hello.c"
    secureos_api_h = root / "user/include/secureos_api.h"
    crt0_src = root / "sdk/lib/crt0.c"
    stubs_src = root / "user/runtime/secureos_api_stubs.c"
    toolchain_lock = root / "build/toolchain.lock"
    sof_wrap_bin = root / "tools/sof_wrap/sof_wrap"

    for p in [hello_src, secureos_api_h, crt0_src, stubs_src, toolchain_lock]:
        if not p.exists():
            raise FileNotFoundError(f"missing required input: {p}")

    if not sof_wrap_bin.exists():
        run(["make", "-C", "tools/sof_wrap"], cwd=root)
    ensure_executable(sof_wrap_bin)

    with tempfile.TemporaryDirectory(prefix="hello-golden-") as td:
        tmp = Path(td)
        hello_o = tmp / "hello.o"
        stubs_o = tmp / "secureos_api_stubs.o"
        hello_elf = tmp / "hello.elf"
        hello_bin = tmp / "hello.bin"

        cflags = [
            "--target=x86_64-unknown-none-elf",
            "-ffreestanding",
            "-fno-stack-protector",
            "-mno-red-zone",
            "-I",
            "user/include",
        ]
        ldflags = ["-m", "elf_x86_64", "-nostdlib", "-e", "main", "--image-base=0x800000"]

        run(["clang", *cflags, "-c", str(hello_src), "-o", str(hello_o)], cwd=root)
        run(["clang", *cflags, "-c", str(stubs_src), "-o", str(stubs_o)], cwd=root)
        run(["ld.lld", *ldflags, "-o", str(hello_elf), str(hello_o), str(stubs_o)], cwd=root)

        run(
            [
                str(sof_wrap_bin),
                "--type",
                "bin",
                "--name",
                "hello",
                "--author",
                "SecureOS",
                "--version",
                "1.0.0",
                "--date",
                sof_date,
                str(hello_elf),
                str(hello_bin),
            ],
            cwd=root,
        )

        lock = json.loads(toolchain_lock.read_text(encoding="utf-8"))
        toolchain_image = lock.get("build", {}).get("imageTag", "")

        return {
            "schemaVersion": 1,
            "_comment": (
                "Host SOF golden pin for dev/hello.c (issue #619). If this drift is intentional, "
                "run `python3 tools/validate_hello_golden.py --refresh` in the same PR and audit the diff."
            ),
            "toolchain": {
                "lockPath": "build/toolchain.lock",
                "imageTag": toolchain_image,
                "lockSha256": sha256_file(toolchain_lock),
            },
            "recipe": {
                "builder": "build_user_app.sh-compatible host path",
                "target": "x86_64-unknown-none-elf",
                "sofWrapDate": sof_date,
                "outputPath": "/apps/hello.bin",
            },
            "inputs": {
                "dev_hello_c": {"path": "dev/hello.c", "sha256": sha256_file(hello_src)},
                "secureos_api_h": {
                    "path": "user/include/secureos_api.h",
                    "sha256": sha256_file(secureos_api_h),
                },
                "crt0_c": {"path": "sdk/lib/crt0.c", "sha256": sha256_file(crt0_src)},
                "secureos_api_stubs_c": {
                    "path": "user/runtime/secureos_api_stubs.c",
                    "sha256": sha256_file(stubs_src),
                },
            },
            "outputs": {
                "hello_bin": {"path": "/apps/hello.bin", "sha256": sha256_file(hello_bin)}
            },
        }


def compare(path: str, expected: str, actual: str, failures: list[str]) -> None:
    if expected != actual:
        failures.append(f"{MARKER}:FAIL:{path}:expected={expected}:actual={actual}")
    else:
        print(f"{MARKER}:PASS:{path}")


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate host hello SOF golden")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent.parent))
    ap.add_argument("--golden", default="tests/m7_toolchain/hello_golden.json")
    ap.add_argument("--refresh", action="store_true")
    args = ap.parse_args()

    root = Path(args.root).resolve()
    golden_path = Path(args.golden)
    if not golden_path.is_absolute():
        golden_path = root / golden_path

    if golden_path.exists():
        current = json.loads(golden_path.read_text(encoding="utf-8"))
        sof_date = current.get("recipe", {}).get("sofWrapDate", DEFAULT_DATE)
    else:
        current = None
        sof_date = DEFAULT_DATE

    computed = compute_state(root, sof_date)

    if args.refresh:
        old = json.dumps(current, sort_keys=True, indent=2) if current is not None else "<none>"
        new = json.dumps(computed, sort_keys=True, indent=2)
        golden_path.parent.mkdir(parents=True, exist_ok=True)
        golden_path.write_text(json.dumps(computed, indent=2) + "\n", encoding="utf-8")
        if old == new:
            print(f"{MARKER}:PASS:refresh:no_change")
        else:
            print(f"{MARKER}:PASS:refresh:updated:{golden_path}")
        return 0

    if current is None:
        print(f"{MARKER}:FAIL:missing_golden:{golden_path}", file=sys.stderr)
        print(f"{MARKER}:HINT:run_with_refresh", file=sys.stderr)
        return 1

    failures: list[str] = []

    compare(
        "toolchain.lockSha256",
        current["toolchain"]["lockSha256"],
        computed["toolchain"]["lockSha256"],
        failures,
    )
    compare(
        "inputs.dev_hello_c.sha256",
        current["inputs"]["dev_hello_c"]["sha256"],
        computed["inputs"]["dev_hello_c"]["sha256"],
        failures,
    )
    compare(
        "inputs.secureos_api_h.sha256",
        current["inputs"]["secureos_api_h"]["sha256"],
        computed["inputs"]["secureos_api_h"]["sha256"],
        failures,
    )
    compare(
        "inputs.crt0_c.sha256",
        current["inputs"]["crt0_c"]["sha256"],
        computed["inputs"]["crt0_c"]["sha256"],
        failures,
    )
    compare(
        "inputs.secureos_api_stubs_c.sha256",
        current["inputs"]["secureos_api_stubs_c"]["sha256"],
        computed["inputs"]["secureos_api_stubs_c"]["sha256"],
        failures,
    )
    compare(
        "outputs.hello_bin.sha256",
        current["outputs"]["hello_bin"]["sha256"],
        computed["outputs"]["hello_bin"]["sha256"],
        failures,
    )

    if failures:
        for f in failures:
            print(f, file=sys.stderr)
        print(f"{MARKER}:FAIL:summary:{len(failures)}", file=sys.stderr)
        return 1

    print(f"{MARKER}:PASS:summary")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"{MARKER}:FAIL:exception:{exc}", file=sys.stderr)
        raise
