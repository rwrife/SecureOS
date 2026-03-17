#!/usr/bin/env python3
"""
Build a minimal ELF32 container whose PT_LOAD segment contains script text.

This matches the layout produced by fs_build_script_elf() in kernel/fs/fs_service.c
so the kernel process loader can unwrap the ELF payload and feed the script to the
existing interpreter.
"""

from pathlib import Path
import struct
import sys

EHDR_SIZE = 52
PHDR_SIZE = 32
SEG_OFFSET = EHDR_SIZE + PHDR_SIZE


def build_script_elf(script_bytes: bytes) -> bytes:
    total_size = SEG_OFFSET + len(script_bytes)
    buf = bytearray(total_size)

    buf[0:4] = b"\x7fELF"
    buf[4] = 1
    buf[5] = 1
    buf[6] = 1

    struct.pack_into("<H", buf, 16, 2)
    struct.pack_into("<H", buf, 18, 3)
    struct.pack_into("<I", buf, 20, 1)
    struct.pack_into("<I", buf, 24, 0x1000)
    struct.pack_into("<I", buf, 28, EHDR_SIZE)
    struct.pack_into("<H", buf, 40, EHDR_SIZE)
    struct.pack_into("<H", buf, 42, PHDR_SIZE)
    struct.pack_into("<H", buf, 44, 1)

    struct.pack_into("<I", buf, EHDR_SIZE + 0, 1)
    struct.pack_into("<I", buf, EHDR_SIZE + 4, SEG_OFFSET)
    struct.pack_into("<I", buf, EHDR_SIZE + 8, 0x1000)
    struct.pack_into("<I", buf, EHDR_SIZE + 12, 0x1000)
    struct.pack_into("<I", buf, EHDR_SIZE + 16, len(script_bytes))
    struct.pack_into("<I", buf, EHDR_SIZE + 20, len(script_bytes))
    struct.pack_into("<I", buf, EHDR_SIZE + 24, 0x4)
    struct.pack_into("<I", buf, EHDR_SIZE + 28, 1)

    buf[SEG_OFFSET:] = script_bytes
    return bytes(buf)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.cmd> <output.elf>", file=sys.stderr)
        return 1

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    script_text = input_path.read_text(encoding="utf-8")
    if not script_text.endswith("\n"):
        script_text += "\n"

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(build_script_elf(script_text.encode("utf-8")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
