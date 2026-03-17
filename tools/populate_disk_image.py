#!/usr/bin/env python3
"""
Populate a SecureOS disk image using the filesystem layout expected by fs_service.c.

The layout intentionally matches fs_initialize_fat32_layout() instead of relying on a
host FAT formatter, because the kernel validates a very small custom FAT-like format.

Usage:
    populate_disk_image.py <disk.img> <blocks> [--os-dir <artifacts/os>] [--lib-dir <artifacts/lib>] [source=target ...]
"""

from pathlib import Path
import struct
import sys

FS_BLOCK_SIZE = 512
FS_DIR_ENTRY_SIZE = 32
FS_DIR_ENTRIES = FS_BLOCK_SIZE // FS_DIR_ENTRY_SIZE
FS_RESERVED_SECTORS = 1
FS_FAT_COUNT = 1
FS_FAT_SIZE_SECTORS = 8
FS_ROOT_CLUSTER = 2
FS_FIRST_DATA_SECTOR = FS_RESERVED_SECTORS + (FS_FAT_COUNT * FS_FAT_SIZE_SECTORS)
FS_FAT_ENTRY_FREE = 0x00000000
FS_FAT_ENTRY_EOC = 0x0FFFFFFF
FS_CLUSTER_MIN_ALLOC = 3
FS_CLUSTER_MAX_ALLOC = 1023
FS_ATTR_DIRECTORY = 0x10
FS_ATTR_ARCHIVE = 0x20


class DiskImage:
    def __init__(self, path: Path, blocks: int) -> None:
        self.path = path
        self.blocks = blocks
        self.data = bytearray(blocks * FS_BLOCK_SIZE)
        self.fat = bytearray(FS_FAT_SIZE_SECTORS * FS_BLOCK_SIZE)
        self._init_layout()

    def _cluster_to_lba(self, cluster: int) -> int:
        return FS_FIRST_DATA_SECTOR + (cluster - FS_ROOT_CLUSTER)

    def _read_u32(self, buffer: bytearray, offset: int) -> int:
        return struct.unpack_from("<I", buffer, offset)[0]

    def _write_u16(self, buffer: bytearray, offset: int, value: int) -> None:
        struct.pack_into("<H", buffer, offset, value)

    def _write_u32(self, buffer: bytearray, offset: int, value: int) -> None:
        struct.pack_into("<I", buffer, offset, value)

    def _read_cluster(self, cluster: int) -> bytearray:
        lba = self._cluster_to_lba(cluster)
        start = lba * FS_BLOCK_SIZE
        return self.data[start:start + FS_BLOCK_SIZE]

    def _write_cluster(self, cluster: int, cluster_data: bytes) -> None:
        lba = self._cluster_to_lba(cluster)
        start = lba * FS_BLOCK_SIZE
        self.data[start:start + FS_BLOCK_SIZE] = cluster_data[:FS_BLOCK_SIZE]

    def _fat_get(self, cluster: int) -> int:
        return self._read_u32(self.fat, cluster * 4) & FS_FAT_ENTRY_EOC

    def _fat_set(self, cluster: int, value: int) -> None:
        self._write_u32(self.fat, cluster * 4, value)

    def _store_fat(self) -> None:
        start = FS_RESERVED_SECTORS * FS_BLOCK_SIZE
        fat_size = FS_FAT_SIZE_SECTORS * FS_BLOCK_SIZE
        self.data[start:start + fat_size] = self.fat

    def _init_layout(self) -> None:
        boot = bytearray(FS_BLOCK_SIZE)
        root = bytearray(FS_BLOCK_SIZE)

        boot[0] = 0xEB
        boot[1] = 0x58
        boot[2] = 0x90
        boot[3:8] = b"MSDOS"
        self._write_u16(boot, 11, FS_BLOCK_SIZE)
        boot[13] = 1
        self._write_u16(boot, 14, FS_RESERVED_SECTORS)
        boot[16] = FS_FAT_COUNT
        self._write_u16(boot, 17, 0)
        self._write_u16(boot, 19, 0)
        boot[21] = 0xF8
        self._write_u16(boot, 22, 0)
        self._write_u16(boot, 24, 63)
        self._write_u16(boot, 26, 255)
        self._write_u32(boot, 28, 0)
        self._write_u32(boot, 32, 128)
        self._write_u32(boot, 36, FS_FAT_SIZE_SECTORS)
        self._write_u16(boot, 40, 0)
        self._write_u16(boot, 42, 0)
        self._write_u32(boot, 44, FS_ROOT_CLUSTER)
        self._write_u16(boot, 48, 1)
        self._write_u16(boot, 50, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        self._write_u32(boot, 67, 0x1234ABCD)
        boot[71:79] = b"SECUREOS"
        boot[82:87] = b"FAT32"
        boot[510] = 0x55
        boot[511] = 0xAA

        self._fat_set(0, 0x0FFFFFF8)
        self._fat_set(1, 0xFFFFFFFF)
        self._fat_set(FS_ROOT_CLUSTER, FS_FAT_ENTRY_EOC)

        self.data[0:FS_BLOCK_SIZE] = boot
        self._store_fat()
        self._write_cluster(FS_ROOT_CLUSTER, root)

    def _parse_83_name(self, name: str) -> bytes:
        if not name:
            raise ValueError("empty name")
        out = bytearray(b"           ")
        seen_dot = False
        dst_name = 0
        dst_ext = 0

        for raw_char in name:
            if raw_char in "/\\":
                raise ValueError(f"invalid path component: {name}")
            if raw_char == ".":
                if seen_dot:
                    raise ValueError(f"invalid 8.3 name: {name}")
                seen_dot = True
                continue

            char = raw_char.upper()
            if not seen_dot:
                if dst_name >= 8:
                    raise ValueError(f"name too long for 8.3 format: {name}")
                out[dst_name] = ord(char)
                dst_name += 1
            else:
                if dst_ext >= 3:
                    raise ValueError(f"extension too long for 8.3 format: {name}")
                out[8 + dst_ext] = ord(char)
                dst_ext += 1

        if dst_name == 0:
            raise ValueError(f"invalid 8.3 name: {name}")
        return bytes(out)

    def _entry_cluster(self, entry: bytes) -> int:
        return (struct.unpack_from("<H", entry, 20)[0] << 16) | struct.unpack_from("<H", entry, 26)[0]

    def _set_entry_cluster(self, entry: bytearray, cluster: int) -> None:
        self._write_u16(entry, 20, (cluster >> 16) & 0xFFFF)
        self._write_u16(entry, 26, cluster & 0xFFFF)

    def _find_free_cluster(self) -> int:
        for cluster in range(FS_CLUSTER_MIN_ALLOC, FS_CLUSTER_MAX_ALLOC + 1):
            if self._fat_get(cluster) == FS_FAT_ENTRY_FREE:
                return cluster
        raise RuntimeError("disk image out of clusters")

    def _find_entry(self, dir_cluster: int, name83: bytes) -> tuple[bytearray, int, bool]:
        dir_data = bytearray(self._read_cluster(dir_cluster))
        first_free = -1

        for index in range(FS_DIR_ENTRIES):
            offset = index * FS_DIR_ENTRY_SIZE
            marker = dir_data[offset]
            if marker == 0x00:
                return dir_data, offset, False
            if marker == 0xE5 or dir_data[offset + 11] == 0x0F:
                if first_free < 0:
                    first_free = offset
                continue
            if dir_data[offset:offset + 11] == name83:
                return dir_data, offset, True

        if first_free >= 0:
            return dir_data, first_free, False
        raise RuntimeError("directory is full")

    def _resolve_dir_cluster(self, path: str) -> int:
        if not path or path == "/":
            return FS_ROOT_CLUSTER
        current = FS_ROOT_CLUSTER
        for component in [part for part in path.replace('\\', '/').split('/') if part]:
            name83 = self._parse_83_name(component)
            dir_data, offset, found = self._find_entry(current, name83)
            if not found:
                raise FileNotFoundError(path)
            entry = dir_data[offset:offset + FS_DIR_ENTRY_SIZE]
            if (entry[11] & FS_ATTR_DIRECTORY) == 0:
                raise NotADirectoryError(path)
            current = self._entry_cluster(entry)
        return current

    def mkdir(self, path: str) -> None:
        normalized = path.replace('\\', '/').strip('/')
        if not normalized:
            return

        parent_path, _, leaf = normalized.rpartition('/')
        parent_cluster = self._resolve_dir_cluster('/' + parent_path) if parent_path else FS_ROOT_CLUSTER
        name83 = self._parse_83_name(leaf)
        parent_dir, offset, found = self._find_entry(parent_cluster, name83)
        if found:
            return

        cluster = self._find_free_cluster()
        self._fat_set(cluster, FS_FAT_ENTRY_EOC)
        self._store_fat()
        self._write_cluster(cluster, bytes(FS_BLOCK_SIZE))

        entry = bytearray(FS_DIR_ENTRY_SIZE)
        entry[0:11] = name83
        entry[11] = FS_ATTR_DIRECTORY
        self._set_entry_cluster(entry, cluster)
        parent_dir[offset:offset + FS_DIR_ENTRY_SIZE] = entry
        self._write_cluster(parent_cluster, parent_dir)

    def write_file(self, path: str, content: bytes) -> None:
        normalized = path.replace('\\', '/').strip('/')
        parent_path, _, leaf = normalized.rpartition('/')
        parent_cluster = self._resolve_dir_cluster('/' + parent_path) if parent_path else FS_ROOT_CLUSTER
        name83 = self._parse_83_name(leaf)
        parent_dir, offset, found = self._find_entry(parent_cluster, name83)

        required_clusters = (len(content) + FS_BLOCK_SIZE - 1) // FS_BLOCK_SIZE
        if required_clusters == 0:
            required_clusters = 1

        chain: list[int] = []

        if found:
            entry = bytearray(parent_dir[offset:offset + FS_DIR_ENTRY_SIZE])
            if (entry[11] & FS_ATTR_DIRECTORY) != 0:
                raise IsADirectoryError(path)
            first_cluster = self._entry_cluster(entry)
            cluster = first_cluster
            while True:
                chain.append(cluster)
                next_cluster = self._fat_get(cluster)
                if next_cluster == FS_FAT_ENTRY_EOC:
                    break
                if next_cluster < FS_CLUSTER_MIN_ALLOC or next_cluster > FS_CLUSTER_MAX_ALLOC:
                    raise RuntimeError(f"invalid FAT chain while writing {path}")
                if next_cluster in chain:
                    raise RuntimeError(f"loop detected in FAT chain while writing {path}")
                cluster = next_cluster
        else:
            first_cluster = self._find_free_cluster()
            self._fat_set(first_cluster, FS_FAT_ENTRY_EOC)
            entry = bytearray(FS_DIR_ENTRY_SIZE)
            entry[0:11] = name83
            entry[11] = FS_ATTR_ARCHIVE
            self._set_entry_cluster(entry, first_cluster)
            chain.append(first_cluster)

        while len(chain) < required_clusters:
            new_cluster = self._find_free_cluster()
            self._fat_set(chain[-1], new_cluster)
            self._fat_set(new_cluster, FS_FAT_ENTRY_EOC)
            chain.append(new_cluster)

        if len(chain) > required_clusters:
            for stale_cluster in chain[required_clusters:]:
                self._fat_set(stale_cluster, FS_FAT_ENTRY_FREE)
                self._write_cluster(stale_cluster, bytes(FS_BLOCK_SIZE))
            self._fat_set(chain[required_clusters - 1], FS_FAT_ENTRY_EOC)
            chain = chain[:required_clusters]

        for index, cluster in enumerate(chain):
            block = bytearray(FS_BLOCK_SIZE)
            start = index * FS_BLOCK_SIZE
            end = min(start + FS_BLOCK_SIZE, len(content))
            if end > start:
                block[0:end - start] = content[start:end]
            self._write_cluster(cluster, block)

        self._store_fat()

        struct.pack_into("<I", entry, 28, len(content))
        parent_dir[offset:offset + FS_DIR_ENTRY_SIZE] = entry
        self._write_cluster(parent_cluster, parent_dir)

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_bytes(self.data)


def main() -> int:
    if len(sys.argv) < 3:
        print(
            f"Usage: {sys.argv[0]} <disk.img> <blocks> [--os-dir <artifacts/os>] [--lib-dir <artifacts/lib>] [source=target ...]",
            file=sys.stderr,
        )
        return 1

    disk_path = Path(sys.argv[1])
    blocks = int(sys.argv[2])
    image = DiskImage(disk_path, blocks)
    image.mkdir('/os')
    image.mkdir('/lib')
    image.write_file('/readme.txt', b'SecureOS filesystem')

    extra_args = sys.argv[3:]
    while extra_args:
        if extra_args[0] == "--os-dir":
            if len(extra_args) < 2:
                raise SystemExit("--os-dir requires a directory path")
            os_dir = Path(extra_args[1])
            for os_file in sorted(os_dir.glob("*.bin")):
                image.write_file(f"/os/{os_file.name}", os_file.read_bytes())
            extra_args = extra_args[2:]
            continue

        if extra_args[0] == "--lib-dir":
            if len(extra_args) < 2:
                raise SystemExit("--lib-dir requires a directory path")
            lib_dir = Path(extra_args[1])
            for lib_file in sorted(lib_dir.glob("*.lib")):
                image.write_file(f"/lib/{lib_file.name}", lib_file.read_bytes())
            extra_args = extra_args[2:]
            continue

        mapping = extra_args[0]
        src_raw, dst_raw = mapping.split('=', 1)
        src = Path(src_raw)
        image.write_file(dst_raw, src.read_bytes())
        extra_args = extra_args[1:]

    image.save()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
