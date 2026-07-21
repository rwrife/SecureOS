#!/usr/bin/env python3
"""
Host test: verify the in-OS toolchain dev directory is staged onto the disk
image correctly (Phase 1 of plans/2026-05-28-in-os-toolchain-self-hosting.md).

Asserts that:
  - the default disk layout creates /apps/dev,
  - nested file targets under /apps/dev are written via auto-created parents,
  - /apps/dev/lib and /apps/dev/tcc are created when staging placeholder files,
  - staged sample/guide/placeholder files round-trip byte-identically,
  - optional host-built /apps/dev/lib archives (`libclib.a`, `libsofpack.a`) are
    staged byte-identically when source artifacts are present.

Runs purely on the host (no QEMU, no toolchain): it drives the same
tools/populate_disk_image.py the real disk build uses, then reads the FAT
structures back to compare contents.

Exit: prints TEST:PASS:in_os_toolchain_dev_dir on success, TEST:FAIL:... and
a non-zero exit code otherwise.
"""

import importlib.util
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
POP_PATH = ROOT / "tools" / "populate_disk_image.py"

_spec = importlib.util.spec_from_file_location("populate_disk_image", POP_PATH)
pop = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(pop)


def read_file(image: "pop.DiskImage", path: str) -> bytes:
    """Read a file back out of an in-memory DiskImage via its FAT chain."""
    normalized = path.replace("\\", "/").strip("/")
    parent_path, _, leaf = normalized.rpartition("/")
    parent_cluster = (
        image._resolve_dir_cluster("/" + parent_path)
        if parent_path
        else pop.FS_ROOT_CLUSTER
    )
    name83 = image._parse_83_name(leaf)
    dir_data, offset, found, _ = image._find_entry(parent_cluster, name83)
    if not found:
        raise FileNotFoundError(path)
    entry = dir_data[offset:offset + pop.FS_DIR_ENTRY_SIZE]
    size = struct.unpack_from("<I", entry, 28)[0]
    cluster = image._entry_cluster(entry)
    out = bytearray()
    seen = set()
    while cluster not in seen:
        seen.add(cluster)
        out.extend(image._read_cluster(cluster))
        nxt = image._fat_get(cluster)
        if nxt == pop.FS_FAT_ENTRY_EOC or nxt < pop.FS_CLUSTER_MIN_ALLOC:
            break
        cluster = nxt
    return bytes(out[:size])


def is_directory(image: "pop.DiskImage", path: str) -> bool:
    normalized = path.replace("\\", "/").strip("/")
    parent_path, _, leaf = normalized.rpartition("/")
    parent_cluster = (
        image._resolve_dir_cluster("/" + parent_path)
        if parent_path
        else pop.FS_ROOT_CLUSTER
    )
    dir_data, offset, found, _ = image._find_entry(
        parent_cluster, image._parse_83_name(leaf)
    )
    if not found:
        return False
    entry = dir_data[offset:offset + pop.FS_DIR_ENTRY_SIZE]
    return (entry[11] & pop.FS_ATTR_DIRECTORY) != 0


def main() -> int:
    failures = []

    # Build an image the same way build_disk_image.sh's main() does.
    image = pop.DiskImage(Path("unused.img"), 16384)
    image.mkdir("/os")
    image.mkdir("/apps")
    image.mkdir("/lib")
    image.mkdir("/scripts")
    image.makedirs("/apps/dev")

    # 1. Default layout creates /apps/dev as a directory.
    if not is_directory(image, "/apps/dev"):
        failures.append("/apps/dev was not created as a directory")

    # 2. Nested write auto-creates parents (simulate a fresh image with no
    #    explicit /apps/dev mkdir, relying solely on write_file).
    fresh = pop.DiskImage(Path("unused2.img"), 16384)
    fresh.mkdir("/apps")
    hello_src = (ROOT / "dev" / "hello.c").read_bytes()
    guide_src = (ROOT / "dev" / "building.txt").read_bytes()
    lib_readme_src = (ROOT / "dev" / "lib" / "README.md").read_bytes()
    tcc_readme_src = (ROOT / "dev" / "tcc" / "README.md").read_bytes()

    libclib_artifact = ROOT / "artifacts" / "user" / "libs" / "libclib.a"
    libsofpack_artifact = ROOT / "artifacts" / "user" / "libs" / "libsofpack.a"
    libclib_src = libclib_artifact.read_bytes() if libclib_artifact.is_file() else None
    libsofpack_src = (
        libsofpack_artifact.read_bytes() if libsofpack_artifact.is_file() else None
    )

    fresh.write_file("/apps/dev/hello.c", hello_src)
    fresh.write_file("/apps/dev/building.txt", guide_src)
    fresh.write_file("/apps/dev/lib/README.md", lib_readme_src)
    fresh.write_file("/apps/dev/tcc/README.md", tcc_readme_src)
    if libclib_src is not None:
        fresh.write_file("/apps/dev/lib/libclib.a", libclib_src)
    if libsofpack_src is not None:
        fresh.write_file("/apps/dev/lib/libsofpack.a", libsofpack_src)

    if not is_directory(fresh, "/apps/dev"):
        failures.append("write_file did not auto-create /apps/dev parent")
    if not is_directory(fresh, "/apps/dev/lib"):
        failures.append("write_file did not auto-create /apps/dev/lib parent")
    if not is_directory(fresh, "/apps/dev/tcc"):
        failures.append("write_file did not auto-create /apps/dev/tcc parent")

    # 3. Staged content round-trips byte-identically.
    got_hello = read_file(fresh, "/apps/dev/hello.c")
    if got_hello != hello_src:
        failures.append(
            f"/apps/dev/hello.c mismatch: wrote {len(hello_src)} bytes, "
            f"read {len(got_hello)} bytes"
        )

    got_guide = read_file(fresh, "/apps/dev/building.txt")
    if got_guide != guide_src:
        failures.append(
            f"/apps/dev/building.txt mismatch: wrote {len(guide_src)} bytes, "
            f"read {len(got_guide)} bytes"
        )

    got_lib_readme = read_file(fresh, "/apps/dev/lib/README.md")
    if got_lib_readme != lib_readme_src:
        failures.append(
            f"/apps/dev/lib/README.md mismatch: wrote {len(lib_readme_src)} bytes, "
            f"read {len(got_lib_readme)} bytes"
        )

    got_tcc_readme = read_file(fresh, "/apps/dev/tcc/README.md")
    if got_tcc_readme != tcc_readme_src:
        failures.append(
            f"/apps/dev/tcc/README.md mismatch: wrote {len(tcc_readme_src)} bytes, "
            f"read {len(got_tcc_readme)} bytes"
        )

    if libclib_src is not None:
        got_libclib = read_file(fresh, "/apps/dev/lib/libclib.a")
        if got_libclib != libclib_src:
            failures.append(
                f"/apps/dev/lib/libclib.a mismatch: wrote {len(libclib_src)} bytes, "
                f"read {len(got_libclib)} bytes"
            )

    if libsofpack_src is not None:
        got_libsofpack = read_file(fresh, "/apps/dev/lib/libsofpack.a")
        if got_libsofpack != libsofpack_src:
            failures.append(
                f"/apps/dev/lib/libsofpack.a mismatch: wrote {len(libsofpack_src)} bytes, "
                f"read {len(got_libsofpack)} bytes"
            )

    # 4. The sample is a non-trivial source file and includes the public API.
    if b"#include \"secureos_api.h\"" not in hello_src:
        failures.append("hello.c does not include secureos_api.h")
    if b"int main" not in hello_src:
        failures.append("hello.c has no main()")

    if failures:
        for f in failures:
            print(f"TEST:FAIL:in_os_toolchain_dev_dir: {f}", file=sys.stderr)
        return 1

    print("TEST:PASS:in_os_toolchain_dev_dir")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
