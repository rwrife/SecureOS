/**
 * @file fs_service.c
 * @brief FAT-like in-memory filesystem service.
 *
 * Purpose:
 *   Provides a minimal FAT-inspired filesystem with directory hierarchy,
 *   file create/read/write/append, directory listing, and ELF binary
 *   loading.  Operates on 512-byte sectors through the storage HAL and
 *   supports persistence via save/load to the underlying block device.
 *
 * Interactions:
 *   - storage_hal.c: all sector-level reads and writes are routed
 *     through the storage HAL abstraction layer.
 *   - app_runtime.c: uses fs_read_file and fs_load_elf to load user-
 *     space application binaries from the filesystem.
 *   - console.c: built-in shell commands (ls, cat, write, mkdir, etc.)
 *     call fs_service functions directly.
 *   - event_bus.c: filesystem operations may trigger audit events.
 *
 * Launched by:
 *   fs_init() is called from kmain() during kernel boot to format the
 *   initial filesystem.  Not a standalone process; compiled into the
 *   kernel image.
 */

#include "fs_service.h"

#include "../hal/storage_hal.h"

enum {
  FS_BLOCK_SIZE = 512,
  FS_DIR_ENTRY_SIZE = 32,
  FS_DIR_ENTRIES = FS_BLOCK_SIZE / FS_DIR_ENTRY_SIZE,

  FS_RESERVED_SECTORS = 1,
  FS_FAT_COUNT = 1,
  FS_FAT_SIZE_SECTORS = 1,
  FS_ROOT_CLUSTER = 2,
  FS_FIRST_DATA_SECTOR = FS_RESERVED_SECTORS + (FS_FAT_COUNT * FS_FAT_SIZE_SECTORS),

  FS_FAT_ENTRY_FREE = 0x00000000u,
  FS_FAT_ENTRY_EOC = 0x0FFFFFFFu,

  FS_CLUSTER_MIN_ALLOC = 3,
  FS_CLUSTER_MAX_ALLOC = 127,

  FS_ATTR_DIRECTORY = 0x10u,
  FS_ATTR_ARCHIVE = 0x20u,

  FS_PATH_COMPONENT_MAX = 16,
  FS_ELF_BUFFER_MAX = 512,
};

static size_t fs_string_len(const char *value) {
  size_t len = 0u;
  if (value == 0) {
    return 0u;
  }
  while (value[len] != '\0') {
    ++len;
  }
  return len;
}

static int fs_char_upper(int value) {
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 'A';
  }
  return value;
}

static int fs_char_lower(int value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A' + 'a';
  }
  return value;
}

static void fs_zero_bytes(uint8_t *dst, size_t size) {
  size_t i = 0u;
  if (dst == 0) {
    return;
  }
  for (i = 0u; i < size; ++i) {
    dst[i] = 0u;
  }
}

static int fs_string_equals(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    if (*left != *right) {
      return 0;
    }
    ++left;
    ++right;
  }

  return *left == *right;
}

static void fs_copy_string(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;

  if (dst == 0 || dst_size == 0u) {
    return;
  }

  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

static uint16_t fs_read_u16(const uint8_t *buffer, size_t offset) {
  return (uint16_t)((uint16_t)buffer[offset] | ((uint16_t)buffer[offset + 1u] << 8u));
}

static uint32_t fs_read_u32(const uint8_t *buffer, size_t offset) {
  return (uint32_t)((uint32_t)buffer[offset] |
                    ((uint32_t)buffer[offset + 1u] << 8u) |
                    ((uint32_t)buffer[offset + 2u] << 16u) |
                    ((uint32_t)buffer[offset + 3u] << 24u));
}

static void fs_write_u16(uint8_t *buffer, size_t offset, uint16_t value) {
  buffer[offset] = (uint8_t)(value & 0xFFu);
  buffer[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
}

static void fs_write_u32(uint8_t *buffer, size_t offset, uint32_t value) {
  buffer[offset] = (uint8_t)(value & 0xFFu);
  buffer[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
  buffer[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
  buffer[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint32_t fs_cluster_to_lba(uint32_t cluster) {
  return FS_FIRST_DATA_SECTOR + (cluster - FS_ROOT_CLUSTER);
}

static int fs_path_is_separator(char value) {
  return value == '/' || value == '\\';
}

static const char *fs_skip_path_separators(const char *cursor) {
  while (*cursor != '\0' && fs_path_is_separator(*cursor)) {
    ++cursor;
  }
  return cursor;
}

static int fs_next_path_component(const char **cursor, char *out_component, size_t out_component_size) {
  size_t i = 0u;
  const char *current = *cursor;

  if (out_component == 0 || out_component_size == 0u) {
    return 0;
  }

  current = fs_skip_path_separators(current);
  if (*current == '\0') {
    *cursor = current;
    out_component[0] = '\0';
    return 0;
  }

  while (*current != '\0' && !fs_path_is_separator(*current)) {
    if (i + 1u >= out_component_size) {
      return 0;
    }
    out_component[i++] = *current++;
  }

  out_component[i] = '\0';
  *cursor = current;
  return 1;
}

static int fs_parse_83_name(const char *path, char name83[11]) {
  size_t src = 0u;
  size_t dst_name = 0u;
  size_t dst_ext = 0u;
  int seen_dot = 0;

  if (path == 0 || path[0] == '\0') {
    return 0;
  }

  for (src = 0u; src < 11u; ++src) {
    name83[src] = ' ';
  }

  src = 0u;
  while (path[src] != '\0') {
    char c = path[src++];

    if (fs_path_is_separator(c)) {
      return 0;
    }
    if (c == '.') {
      if (seen_dot) {
        return 0;
      }
      seen_dot = 1;
      continue;
    }

    c = (char)fs_char_upper(c);
    if (!seen_dot) {
      if (dst_name >= 8u) {
        return 0;
      }
      name83[dst_name++] = c;
    } else {
      if (dst_ext >= 3u) {
        return 0;
      }
      name83[8u + dst_ext++] = c;
    }
  }

  return dst_name > 0u;
}

static void fs_format_name_for_list(const uint8_t *entry, char *out, size_t out_size, int append_slash) {
  size_t cursor = 0u;
  size_t i = 0u;
  int has_ext = 0;

  if (out == 0 || out_size == 0u) {
    return;
  }

  for (i = 0u; i < 8u; ++i) {
    if (entry[i] == ' ') {
      break;
    }
    if (cursor + 1u < out_size) {
      out[cursor++] = (char)fs_char_lower(entry[i]);
    }
  }

  for (i = 8u; i < 11u; ++i) {
    if (entry[i] != ' ') {
      has_ext = 1;
      break;
    }
  }

  if (has_ext && cursor + 1u < out_size) {
    out[cursor++] = '.';
  }
  if (has_ext) {
    for (i = 8u; i < 11u; ++i) {
      if (entry[i] == ' ') {
        break;
      }
      if (cursor + 1u < out_size) {
        out[cursor++] = (char)fs_char_lower(entry[i]);
      }
    }
  }

  if (append_slash && cursor + 1u < out_size) {
    out[cursor++] = '/';
  }

  out[cursor] = '\0';
}

static int fs_entry_name_matches(const uint8_t *entry, const char name83[11]) {
  size_t i = 0u;
  for (i = 0u; i < 11u; ++i) {
    if (entry[i] != (uint8_t)name83[i]) {
      return 0;
    }
  }
  return 1;
}

static uint32_t fs_entry_cluster(const uint8_t *entry) {
  return ((uint32_t)fs_read_u16(entry, 20u) << 16u) | (uint32_t)fs_read_u16(entry, 26u);
}

static void fs_set_entry_cluster(uint8_t *entry, uint32_t cluster) {
  fs_write_u16(entry, 20u, (uint16_t)((cluster >> 16u) & 0xFFFFu));
  fs_write_u16(entry, 26u, (uint16_t)(cluster & 0xFFFFu));
}

static fs_result_t fs_read_sector(uint32_t lba, uint8_t *buffer) {
  if (storage_hal_read(lba, buffer, FS_BLOCK_SIZE) != STORAGE_OK) {
    return FS_ERR_STORAGE;
  }
  return FS_OK;
}

static fs_result_t fs_write_sector(uint32_t lba, const uint8_t *buffer) {
  if (storage_hal_write(lba, buffer, FS_BLOCK_SIZE) != STORAGE_OK) {
    return FS_ERR_STORAGE;
  }
  return FS_OK;
}

static fs_result_t fs_read_cluster(uint32_t cluster, uint8_t *buffer) {
  if (cluster < FS_ROOT_CLUSTER || cluster > FS_CLUSTER_MAX_ALLOC) {
    return FS_ERR_INVALID_ARG;
  }
  return fs_read_sector(fs_cluster_to_lba(cluster), buffer);
}

static fs_result_t fs_write_cluster(uint32_t cluster, const uint8_t *buffer) {
  if (cluster < FS_ROOT_CLUSTER || cluster > FS_CLUSTER_MAX_ALLOC) {
    return FS_ERR_INVALID_ARG;
  }
  return fs_write_sector(fs_cluster_to_lba(cluster), buffer);
}

static fs_result_t fs_load_fat(uint8_t fat[FS_BLOCK_SIZE]) {
  return fs_read_sector(FS_RESERVED_SECTORS, fat);
}

static fs_result_t fs_store_fat(const uint8_t fat[FS_BLOCK_SIZE]) {
  return fs_write_sector(FS_RESERVED_SECTORS, fat);
}

static uint32_t fs_fat_get_entry(const uint8_t fat[FS_BLOCK_SIZE], uint32_t cluster) {
  return fs_read_u32(fat, (size_t)cluster * 4u) & FS_FAT_ENTRY_EOC;
}

static void fs_fat_set_entry(uint8_t fat[FS_BLOCK_SIZE], uint32_t cluster, uint32_t value) {
  fs_write_u32(fat, (size_t)cluster * 4u, value);
}

static fs_result_t fs_find_free_cluster(const uint8_t fat[FS_BLOCK_SIZE], uint32_t *out_cluster) {
  uint32_t cluster = 0u;

  if (out_cluster == 0) {
    return FS_ERR_INVALID_ARG;
  }

  for (cluster = FS_CLUSTER_MIN_ALLOC; cluster <= FS_CLUSTER_MAX_ALLOC; ++cluster) {
    if (fs_fat_get_entry(fat, cluster) == FS_FAT_ENTRY_FREE) {
      *out_cluster = cluster;
      return FS_OK;
    }
  }

  return FS_ERR_NO_SPACE;
}

static fs_result_t fs_find_entry_in_dir(uint32_t dir_cluster,
                                        const char name83[11],
                                        uint8_t dir_data[FS_BLOCK_SIZE],
                                        uint32_t *out_offset,
                                        int *out_found) {
  size_t i = 0u;

  if (name83 == 0 || out_offset == 0 || out_found == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_read_cluster(dir_cluster, dir_data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
    size_t off = i * FS_DIR_ENTRY_SIZE;
    uint8_t marker = dir_data[off];

    if (marker == 0x00u) {
      *out_offset = (uint32_t)off;
      *out_found = 0;
      return FS_OK;
    }

    if (marker == 0xE5u || dir_data[off + 11u] == 0x0Fu) {
      continue;
    }

    if (fs_entry_name_matches(&dir_data[off], name83)) {
      *out_offset = (uint32_t)off;
      *out_found = 1;
      return FS_OK;
    }
  }

  return FS_ERR_NO_SPACE;
}

static fs_result_t fs_resolve_dir_cluster(const char *path, uint32_t *out_cluster) {
  const char *cursor = path;
  uint32_t current_cluster = FS_ROOT_CLUSTER;
  char component[FS_PATH_COMPONENT_MAX];

  if (out_cluster == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (path == 0 || path[0] == '\0' || fs_string_equals(path, "/")) {
    *out_cluster = FS_ROOT_CLUSTER;
    return FS_OK;
  }

  while (fs_next_path_component(&cursor, component, sizeof(component))) {
    char name83[11];
    uint8_t dir_data[FS_BLOCK_SIZE];
    uint32_t offset = 0u;
    int found = 0;

    if (!fs_parse_83_name(component, name83)) {
      return FS_ERR_INVALID_ARG;
    }

    if (fs_find_entry_in_dir(current_cluster, name83, dir_data, &offset, &found) != FS_OK) {
      return FS_ERR_NOT_FOUND;
    }
    if (!found) {
      return FS_ERR_NOT_FOUND;
    }
    if ((dir_data[offset + 11u] & FS_ATTR_DIRECTORY) == 0u) {
      return FS_ERR_NOT_DIR;
    }

    current_cluster = fs_entry_cluster(&dir_data[offset]);
  }

  *out_cluster = current_cluster;
  return FS_OK;
}

static fs_result_t fs_resolve_parent_and_leaf(const char *path,
                                              uint32_t *out_parent_cluster,
                                              char out_name83[11]) {
  const char *cursor = path;
  char component[FS_PATH_COMPONENT_MAX];
  char pending_leaf[FS_PATH_COMPONENT_MAX];
  int have_leaf = 0;
  uint32_t current_cluster = FS_ROOT_CLUSTER;

  if (path == 0 || out_parent_cluster == 0 || out_name83 == 0) {
    return FS_ERR_INVALID_ARG;
  }

  while (fs_next_path_component(&cursor, component, sizeof(component))) {
    if (!have_leaf) {
      fs_copy_string(pending_leaf, sizeof(pending_leaf), component);
      have_leaf = 1;
      continue;
    }

    {
      char name83[11];
      uint8_t dir_data[FS_BLOCK_SIZE];
      uint32_t offset = 0u;
      int found = 0;

      if (!fs_parse_83_name(pending_leaf, name83)) {
        return FS_ERR_INVALID_ARG;
      }

      if (fs_find_entry_in_dir(current_cluster, name83, dir_data, &offset, &found) != FS_OK || !found) {
        return FS_ERR_NOT_FOUND;
      }
      if ((dir_data[offset + 11u] & FS_ATTR_DIRECTORY) == 0u) {
        return FS_ERR_NOT_DIR;
      }

      current_cluster = fs_entry_cluster(&dir_data[offset]);
      fs_copy_string(pending_leaf, sizeof(pending_leaf), component);
    }
  }

  if (!have_leaf) {
    return FS_ERR_INVALID_ARG;
  }

  if (!fs_parse_83_name(pending_leaf, out_name83)) {
    return FS_ERR_INVALID_ARG;
  }

  *out_parent_cluster = current_cluster;
  return FS_OK;
}

static fs_result_t fs_initialize_fat32_layout(void) {
  uint8_t boot[FS_BLOCK_SIZE];
  uint8_t fat[FS_BLOCK_SIZE];
  uint8_t root[FS_BLOCK_SIZE];

  fs_zero_bytes(boot, sizeof(boot));
  fs_zero_bytes(fat, sizeof(fat));
  fs_zero_bytes(root, sizeof(root));

  boot[0u] = 0xEBu;
  boot[1u] = 0x58u;
  boot[2u] = 0x90u;
  boot[3u] = 'M';
  boot[4u] = 'S';
  boot[5u] = 'D';
  boot[6u] = 'O';
  boot[7u] = 'S';

  fs_write_u16(boot, 11u, FS_BLOCK_SIZE);
  boot[13u] = 1u;
  fs_write_u16(boot, 14u, FS_RESERVED_SECTORS);
  boot[16u] = FS_FAT_COUNT;
  fs_write_u16(boot, 17u, 0u);
  fs_write_u16(boot, 19u, 0u);
  boot[21u] = 0xF8u;
  fs_write_u16(boot, 22u, 0u);
  fs_write_u16(boot, 24u, 63u);
  fs_write_u16(boot, 26u, 255u);
  fs_write_u32(boot, 28u, 0u);
  fs_write_u32(boot, 32u, 128u);
  fs_write_u32(boot, 36u, FS_FAT_SIZE_SECTORS);
  fs_write_u16(boot, 40u, 0u);
  fs_write_u16(boot, 42u, 0u);
  fs_write_u32(boot, 44u, FS_ROOT_CLUSTER);
  fs_write_u16(boot, 48u, 1u);
  fs_write_u16(boot, 50u, 6u);
  boot[64u] = 0x80u;
  boot[66u] = 0x29u;
  fs_write_u32(boot, 67u, 0x1234ABCDu);
  boot[71u] = 'S';
  boot[72u] = 'E';
  boot[73u] = 'C';
  boot[74u] = 'U';
  boot[75u] = 'R';
  boot[76u] = 'E';
  boot[77u] = 'O';
  boot[78u] = 'S';
  boot[82u] = 'F';
  boot[83u] = 'A';
  boot[84u] = 'T';
  boot[85u] = '3';
  boot[86u] = '2';
  boot[510u] = 0x55u;
  boot[511u] = 0xAAu;

  fs_fat_set_entry(fat, 0u, 0x0FFFFFF8u);
  fs_fat_set_entry(fat, 1u, 0xFFFFFFFFu);
  fs_fat_set_entry(fat, FS_ROOT_CLUSTER, FS_FAT_ENTRY_EOC);

  if (fs_write_sector(0u, boot) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_store_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_write_cluster(FS_ROOT_CLUSTER, root) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  return FS_OK;
}

static int fs_is_fat32_boot_sector_valid(const uint8_t boot[FS_BLOCK_SIZE]) {
  if (fs_read_u16(boot, 11u) != FS_BLOCK_SIZE) {
    return 0;
  }
  if (boot[13u] != 1u) {
    return 0;
  }
  if (fs_read_u16(boot, 14u) != FS_RESERVED_SECTORS) {
    return 0;
  }
  if (boot[16u] != FS_FAT_COUNT) {
    return 0;
  }
  if (fs_read_u32(boot, 36u) != FS_FAT_SIZE_SECTORS) {
    return 0;
  }
  if (fs_read_u32(boot, 44u) != FS_ROOT_CLUSTER) {
    return 0;
  }
  if (boot[510u] != 0x55u || boot[511u] != 0xAAu) {
    return 0;
  }
  return 1;
}

static fs_result_t fs_ensure_initialized(void) {
  uint8_t boot[FS_BLOCK_SIZE];

  if (!storage_hal_ready()) {
    return FS_ERR_STORAGE;
  }

  if (fs_read_sector(0u, boot) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (!fs_is_fat32_boot_sector_valid(boot)) {
    return fs_initialize_fat32_layout();
  }

  return FS_OK;
}

static fs_result_t fs_write_entry_content(uint32_t first_cluster,
                                          const uint8_t *content,
                                          size_t content_len,
                                          int append,
                                          uint32_t *out_size) {
  uint8_t data[FS_BLOCK_SIZE];
  size_t start = 0u;
  size_t i = 0u;

  if (first_cluster < FS_CLUSTER_MIN_ALLOC || first_cluster > FS_CLUSTER_MAX_ALLOC) {
    return FS_ERR_INVALID_ARG;
  }

  if (content_len > FS_BLOCK_SIZE) {
    return FS_ERR_NO_SPACE;
  }

  if (fs_read_cluster(first_cluster, data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (append && out_size != 0) {
    start = *out_size;
  }

  if (start + content_len > FS_BLOCK_SIZE) {
    return FS_ERR_NO_SPACE;
  }

  if (!append) {
    fs_zero_bytes(data, sizeof(data));
    start = 0u;
  }

  for (i = 0u; i < content_len; ++i) {
    data[start + i] = content[i];
  }

  if (fs_write_cluster(first_cluster, data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (out_size != 0) {
    *out_size = (uint32_t)(start + content_len);
  }

  return FS_OK;
}

static fs_result_t fs_build_script_elf(const char *script,
                                       uint8_t *out_buffer,
                                       size_t out_buffer_size,
                                       size_t *out_len) {
  const size_t ehdr_size = 52u;
  const size_t phdr_size = 32u;
  const size_t seg_offset = ehdr_size + phdr_size;
  const size_t script_len = fs_string_len(script);
  size_t i = 0u;

  if (script == 0 || out_buffer == 0 || out_len == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (seg_offset + script_len > out_buffer_size) {
    return FS_ERR_NO_SPACE;
  }

  fs_zero_bytes(out_buffer, out_buffer_size);

  out_buffer[0u] = 0x7Fu;
  out_buffer[1u] = 'E';
  out_buffer[2u] = 'L';
  out_buffer[3u] = 'F';
  out_buffer[4u] = 1u;
  out_buffer[5u] = 1u;
  out_buffer[6u] = 1u;

  fs_write_u16(out_buffer, 16u, 2u);
  fs_write_u16(out_buffer, 18u, 3u);
  fs_write_u32(out_buffer, 20u, 1u);
  fs_write_u32(out_buffer, 24u, 0x1000u);
  fs_write_u32(out_buffer, 28u, (uint32_t)ehdr_size);
  fs_write_u16(out_buffer, 40u, (uint16_t)ehdr_size);
  fs_write_u16(out_buffer, 42u, (uint16_t)phdr_size);
  fs_write_u16(out_buffer, 44u, 1u);

  fs_write_u32(out_buffer, ehdr_size + 0u, 1u);
  fs_write_u32(out_buffer, ehdr_size + 4u, (uint32_t)seg_offset);
  fs_write_u32(out_buffer, ehdr_size + 8u, 0x1000u);
  fs_write_u32(out_buffer, ehdr_size + 12u, 0x1000u);
  fs_write_u32(out_buffer, ehdr_size + 16u, (uint32_t)script_len);
  fs_write_u32(out_buffer, ehdr_size + 20u, (uint32_t)script_len);
  fs_write_u32(out_buffer, ehdr_size + 24u, 0x4u);
  fs_write_u32(out_buffer, ehdr_size + 28u, 1u);

  for (i = 0u; i < script_len; ++i) {
    out_buffer[seg_offset + i] = (uint8_t)script[i];
  }

  *out_len = seg_offset + script_len;
  return FS_OK;
}

void fs_service_init(void) {
  uint8_t elf_blob[FS_ELF_BUFFER_MAX];
  size_t elf_len = 0u;

  if (fs_ensure_initialized() != FS_OK) {
    return;
  }

  (void)fs_mkdir("os");
  (void)fs_mkdir("apps");
  (void)fs_mkdir("lib");
  (void)fs_write_file("readme.txt", "SecureOS filesystem", 0);

  if (fs_build_script_elf(
          "print commands: help, ping, echo <text>, ls [dir], cat <file>, write <file> <text>, append <file> <text>, mkdir <dir>, cd <dir>, clear, env [key[=value]|key value], session [list|new|switch <id>], storage, apps, libs [loaded|use <h>|release <h>], loadlib <lib>, unload <handle>, run <app>, exit <pass|fail>\n",
          elf_blob,
          sizeof(elf_blob),
          &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/help.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("print pong\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/ping.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("print $ARGS\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/echo.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("ls $ARGS\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/ls.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("cat $1\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/cat.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("write $1 $2\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/write.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("append $1 $2\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/append.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("mkdir $1\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/mkdir.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("cd $1\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/cd.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("env $ARGS\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/env.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("apps\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/apps.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("libs $ARGS\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/libs.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("loadlib $1\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/loadlib.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("unloadlib $1\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/unload.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("storage\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("os/storage.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("print envlib\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("lib/envlib.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("print fslib\n", elf_blob, sizeof(elf_blob), &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("lib/fslib.elf", elf_blob, elf_len, 0);
  }

  if (fs_build_script_elf("print [filedemo] start\n"
          "ls /\n"
          "cat /readme.txt\n"
          "write appdemo.txt filedemo\n"
          "append appdemo.txt -updated\n"
          "print [filedemo] wrote appdemo.txt\n"
          "print [filedemo] done\n",
          elf_blob,
          sizeof(elf_blob),
          &elf_len) == FS_OK) {
    (void)fs_write_file_bytes("apps/filedemo.elf", elf_blob, elf_len, 0);
  }
}

fs_result_t fs_list_root(char *out_buffer, size_t out_buffer_size, size_t *out_len) {
  return fs_list_dir("/", out_buffer, out_buffer_size, out_len);
}

fs_result_t fs_list_dir(const char *path, char *out_buffer, size_t out_buffer_size, size_t *out_len) {
  uint8_t dir_data[FS_BLOCK_SIZE];
  uint32_t dir_cluster = FS_ROOT_CLUSTER;
  size_t cursor = 0u;
  size_t i = 0u;

  if (out_buffer == 0 || out_len == 0 || out_buffer_size == 0u) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_resolve_dir_cluster(path, &dir_cluster) != FS_OK) {
    return FS_ERR_NOT_FOUND;
  }

  if (fs_read_cluster(dir_cluster, dir_data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  out_buffer[0] = '\0';

  for (i = 0u; i < FS_DIR_ENTRIES; ++i) {
    size_t off = i * FS_DIR_ENTRY_SIZE;
    uint8_t marker = dir_data[off];
    int is_dir = 0;
    char formatted[16];
    size_t j = 0u;

    if (marker == 0x00u) {
      break;
    }
    if (marker == 0xE5u || dir_data[off + 11u] == 0x0Fu) {
      continue;
    }

    is_dir = (dir_data[off + 11u] & FS_ATTR_DIRECTORY) != 0u;
    fs_format_name_for_list(&dir_data[off], formatted, sizeof(formatted), is_dir);

    while (formatted[j] != '\0') {
      if (cursor + 1u >= out_buffer_size) {
        return FS_ERR_NO_SPACE;
      }
      out_buffer[cursor++] = formatted[j++];
    }
    if (cursor + 1u >= out_buffer_size) {
      return FS_ERR_NO_SPACE;
    }
    out_buffer[cursor++] = '\n';
  }

  out_buffer[cursor] = '\0';
  *out_len = cursor;
  return FS_OK;
}

fs_result_t fs_read_file(const char *path, char *out_buffer, size_t out_buffer_size, size_t *out_len) {
  fs_result_t result = FS_OK;
  size_t bytes_len = 0u;

  if (out_buffer == 0 || out_len == 0 || out_buffer_size == 0u) {
    return FS_ERR_INVALID_ARG;
  }

  result = fs_read_file_bytes(path, (uint8_t *)out_buffer, out_buffer_size - 1u, &bytes_len);
  if (result != FS_OK) {
    return result;
  }

  out_buffer[bytes_len] = '\0';
  *out_len = bytes_len;
  return FS_OK;
}

fs_result_t fs_read_file_bytes(const char *path,
                               uint8_t *out_buffer,
                               size_t out_buffer_size,
                               size_t *out_len) {
  uint8_t parent_dir[FS_BLOCK_SIZE];
  uint8_t file_data[FS_BLOCK_SIZE];
  uint32_t parent_cluster = FS_ROOT_CLUSTER;
  char name83[11];
  uint32_t offset = 0u;
  int found = 0;
  uint32_t first_cluster = 0u;
  uint32_t file_size = 0u;
  size_t i = 0u;

  if (path == 0 || out_buffer == 0 || out_len == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_resolve_parent_and_leaf(path, &parent_cluster, name83) != FS_OK) {
    return FS_ERR_NOT_FOUND;
  }

  if (fs_find_entry_in_dir(parent_cluster, name83, parent_dir, &offset, &found) != FS_OK || !found) {
    return FS_ERR_NOT_FOUND;
  }

  if ((parent_dir[offset + 11u] & FS_ATTR_DIRECTORY) != 0u) {
    return FS_ERR_NOT_DIR;
  }

  first_cluster = fs_entry_cluster(&parent_dir[offset]);
  file_size = fs_read_u32(parent_dir, offset + 28u);

  if (file_size > out_buffer_size || file_size > FS_BLOCK_SIZE) {
    return FS_ERR_NO_SPACE;
  }

  if (fs_read_cluster(first_cluster, file_data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  for (i = 0u; i < file_size; ++i) {
    out_buffer[i] = file_data[i];
  }

  *out_len = (size_t)file_size;
  return FS_OK;
}

fs_result_t fs_write_file(const char *path, const char *content, int append) {
  if (content == 0) {
    return FS_ERR_INVALID_ARG;
  }

  return fs_write_file_bytes(path, (const uint8_t *)content, fs_string_len(content), append);
}

fs_result_t fs_write_file_bytes(const char *path,
                                const uint8_t *content,
                                size_t content_len,
                                int append) {
  uint8_t parent_dir[FS_BLOCK_SIZE];
  uint8_t fat[FS_BLOCK_SIZE];
  uint32_t parent_cluster = FS_ROOT_CLUSTER;
  char name83[11];
  uint32_t offset = 0u;
  int found = 0;
  uint32_t first_cluster = 0u;
  uint32_t file_size = 0u;
  fs_result_t result = FS_OK;

  if (path == 0 || content == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_load_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  result = fs_resolve_parent_and_leaf(path, &parent_cluster, name83);
  if (result != FS_OK) {
    return result;
  }

  result = fs_find_entry_in_dir(parent_cluster, name83, parent_dir, &offset, &found);
  if (result == FS_ERR_NO_SPACE) {
    return FS_ERR_NO_SPACE;
  }
  if (result != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (!found) {
    size_t i = 0u;

    if (offset + FS_DIR_ENTRY_SIZE > FS_BLOCK_SIZE) {
      return FS_ERR_NO_SPACE;
    }

    result = fs_find_free_cluster(fat, &first_cluster);
    if (result != FS_OK) {
      return result;
    }

    fs_fat_set_entry(fat, first_cluster, FS_FAT_ENTRY_EOC);

    for (i = 0u; i < 11u; ++i) {
      parent_dir[offset + i] = (uint8_t)name83[i];
    }
    parent_dir[offset + 11u] = FS_ATTR_ARCHIVE;
    fs_set_entry_cluster(&parent_dir[offset], first_cluster);
    file_size = 0u;
  } else {
    first_cluster = fs_entry_cluster(&parent_dir[offset]);
    file_size = fs_read_u32(parent_dir, offset + 28u);

    if ((parent_dir[offset + 11u] & FS_ATTR_DIRECTORY) != 0u) {
      return FS_ERR_NOT_DIR;
    }

    if (first_cluster < FS_CLUSTER_MIN_ALLOC || first_cluster > FS_CLUSTER_MAX_ALLOC) {
      return FS_ERR_STORAGE;
    }
  }

  result = fs_write_entry_content(first_cluster, content, content_len, append, &file_size);
  if (result != FS_OK) {
    return result;
  }

  fs_write_u32(parent_dir, offset + 28u, file_size);

  if (fs_store_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_write_cluster(parent_cluster, parent_dir) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  return FS_OK;
}

fs_result_t fs_mkdir(const char *path) {
  uint8_t parent_dir[FS_BLOCK_SIZE];
  uint8_t child_dir[FS_BLOCK_SIZE];
  uint8_t fat[FS_BLOCK_SIZE];
  uint32_t parent_cluster = FS_ROOT_CLUSTER;
  char name83[11];
  uint32_t offset = 0u;
  uint32_t new_cluster = 0u;
  int found = 0;
  fs_result_t result = FS_OK;

  if (path == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_load_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  result = fs_resolve_parent_and_leaf(path, &parent_cluster, name83);
  if (result != FS_OK) {
    return result;
  }

  result = fs_find_entry_in_dir(parent_cluster, name83, parent_dir, &offset, &found);
  if (result == FS_ERR_NO_SPACE) {
    return FS_ERR_NO_SPACE;
  }
  if (result != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (found) {
    if ((parent_dir[offset + 11u] & FS_ATTR_DIRECTORY) != 0u) {
      return FS_ERR_ALREADY_EXISTS;
    }
    return FS_ERR_NOT_DIR;
  }

  result = fs_find_free_cluster(fat, &new_cluster);
  if (result != FS_OK) {
    return result;
  }

  fs_fat_set_entry(fat, new_cluster, FS_FAT_ENTRY_EOC);

  for (size_t i = 0u; i < 11u; ++i) {
    parent_dir[offset + i] = (uint8_t)name83[i];
  }
  parent_dir[offset + 11u] = FS_ATTR_DIRECTORY;
  fs_set_entry_cluster(&parent_dir[offset], new_cluster);
  fs_write_u32(parent_dir, offset + 28u, 0u);

  fs_zero_bytes(child_dir, sizeof(child_dir));

  if (fs_store_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_write_cluster(parent_cluster, parent_dir) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_write_cluster(new_cluster, child_dir) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  return FS_OK;
}
