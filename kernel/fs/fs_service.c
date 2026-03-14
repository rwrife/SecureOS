#include "fs_service.h"

#include "../hal/storage_hal.h"

enum {
  FS_BLOCK_SIZE = 512,
  FS_DIR_ENTRY_SIZE = 32,
  FS_ROOT_DIR_ENTRIES = FS_BLOCK_SIZE / FS_DIR_ENTRY_SIZE,

  FS_RESERVED_SECTORS = 1,
  FS_FAT_COUNT = 1,
  FS_FAT_SIZE_SECTORS = 1,
  FS_ROOT_CLUSTER = 2,
  FS_FIRST_DATA_SECTOR = FS_RESERVED_SECTORS + (FS_FAT_COUNT * FS_FAT_SIZE_SECTORS),

  FS_FAT_ENTRY_FREE = 0x00000000u,
  FS_FAT_ENTRY_EOC = 0x0FFFFFFFu,

  FS_CLUSTER_MIN_ALLOC = 3,
  FS_CLUSTER_MAX_ALLOC = 127,
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

    if (c == '/' || c == '\\') {
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

static void fs_format_name_for_list(const uint8_t *entry, char *out, size_t out_size) {
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

static fs_result_t fs_find_free_cluster(uint32_t *out_cluster) {
  uint8_t fat[FS_BLOCK_SIZE];
  uint32_t cluster = 0u;

  if (out_cluster == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_load_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  for (cluster = FS_CLUSTER_MIN_ALLOC; cluster <= FS_CLUSTER_MAX_ALLOC; ++cluster) {
    if (fs_fat_get_entry(fat, cluster) == FS_FAT_ENTRY_FREE) {
      *out_cluster = cluster;
      return FS_OK;
    }
  }

  return FS_ERR_NO_SPACE;
}

static fs_result_t fs_find_dir_entry(const char *path,
                                     uint8_t root[FS_BLOCK_SIZE],
                                     uint32_t *out_offset,
                                     int *out_found) {
  char name83[11];
  size_t i = 0u;

  if (out_offset == 0 || out_found == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (!fs_parse_83_name(path, name83)) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_read_sector(fs_cluster_to_lba(FS_ROOT_CLUSTER), root) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  for (i = 0u; i < FS_ROOT_DIR_ENTRIES; ++i) {
    size_t off = i * FS_DIR_ENTRY_SIZE;
    uint8_t marker = root[off];

    if (marker == 0x00u) {
      *out_offset = (uint32_t)off;
      *out_found = 0;
      return FS_OK;
    }
    if (marker == 0xE5u) {
      continue;
    }
    if (root[off + 11u] == 0x0Fu) {
      continue;
    }

    if (fs_entry_name_matches(&root[off], name83)) {
      *out_offset = (uint32_t)off;
      *out_found = 1;
      return FS_OK;
    }
  }

  return FS_ERR_NO_SPACE;
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
  if (fs_write_sector(fs_cluster_to_lba(FS_ROOT_CLUSTER), root) != FS_OK) {
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
                                          const char *content,
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

  if (fs_read_sector(fs_cluster_to_lba(first_cluster), data) != FS_OK) {
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
    data[start + i] = (uint8_t)content[i];
  }

  if (fs_write_sector(fs_cluster_to_lba(first_cluster), data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (out_size != 0) {
    *out_size = (uint32_t)(start + content_len);
  }

  return FS_OK;
}

void fs_service_init(void) {
  if (fs_ensure_initialized() != FS_OK) {
    return;
  }

  (void)fs_write_file("readme.txt", "SecureOS FAT32 demo filesystem", 0);
  (void)fs_write_file("notes.txt", "type help", 0);
}

fs_result_t fs_list_root(char *out_buffer, size_t out_buffer_size, size_t *out_len) {
  uint8_t root[FS_BLOCK_SIZE];
  size_t cursor = 0u;
  size_t i = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u || out_len == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_read_sector(fs_cluster_to_lba(FS_ROOT_CLUSTER), root) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  out_buffer[0] = '\0';

  for (i = 0u; i < FS_ROOT_DIR_ENTRIES; ++i) {
    size_t off = i * FS_DIR_ENTRY_SIZE;
    uint8_t marker = root[off];
    char formatted[16];
    size_t j = 0u;

    if (marker == 0x00u) {
      break;
    }
    if (marker == 0xE5u || root[off + 11u] == 0x0Fu) {
      continue;
    }

    fs_format_name_for_list(&root[off], formatted, sizeof(formatted));
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
  uint8_t root[FS_BLOCK_SIZE];
  uint8_t data[FS_BLOCK_SIZE];
  uint32_t offset = 0u;
  int found = 0;
  uint32_t first_cluster = 0u;
  uint32_t file_size = 0u;
  size_t i = 0u;

  if (path == 0 || out_buffer == 0 || out_len == 0 || out_buffer_size == 0u) {
    return FS_ERR_INVALID_ARG;
  }

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_find_dir_entry(path, root, &offset, &found) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (!found) {
    return FS_ERR_NOT_FOUND;
  }

  first_cluster = ((uint32_t)fs_read_u16(root, offset + 20u) << 16u) |
                  (uint32_t)fs_read_u16(root, offset + 26u);
  file_size = fs_read_u32(root, offset + 28u);

  if (file_size + 1u > out_buffer_size || file_size > FS_BLOCK_SIZE) {
    return FS_ERR_NO_SPACE;
  }

  if (fs_read_sector(fs_cluster_to_lba(first_cluster), data) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  for (i = 0u; i < file_size; ++i) {
    out_buffer[i] = (char)data[i];
  }
  out_buffer[file_size] = '\0';
  *out_len = (size_t)file_size;
  return FS_OK;
}

fs_result_t fs_write_file(const char *path, const char *content, int append) {
  uint8_t root[FS_BLOCK_SIZE];
  uint8_t fat[FS_BLOCK_SIZE];
  char name83[11];
  uint32_t offset = 0u;
  int found = 0;
  uint32_t first_cluster = 0u;
  uint32_t file_size = 0u;
  size_t content_len = 0u;
  fs_result_t result = FS_OK;

  if (path == 0 || content == 0) {
    return FS_ERR_INVALID_ARG;
  }

  if (!fs_parse_83_name(path, name83)) {
    return FS_ERR_INVALID_ARG;
  }

  content_len = fs_string_len(content);

  if (fs_ensure_initialized() != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (fs_load_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  result = fs_find_dir_entry(path, root, &offset, &found);
  if (result == FS_ERR_NO_SPACE) {
    return FS_ERR_NO_SPACE;
  }
  if (result != FS_OK) {
    return FS_ERR_STORAGE;
  }

  if (!found) {
    if (offset + FS_DIR_ENTRY_SIZE > FS_BLOCK_SIZE) {
      return FS_ERR_NO_SPACE;
    }

    result = fs_find_free_cluster(&first_cluster);
    if (result != FS_OK) {
      return result;
    }

    fs_fat_set_entry(fat, first_cluster, FS_FAT_ENTRY_EOC);

    for (size_t i = 0u; i < 11u; ++i) {
      root[offset + i] = (uint8_t)name83[i];
    }
    root[offset + 11u] = 0x20u;
    fs_write_u16(root, offset + 20u, (uint16_t)((first_cluster >> 16u) & 0xFFFFu));
    fs_write_u16(root, offset + 26u, (uint16_t)(first_cluster & 0xFFFFu));
    file_size = 0u;
  } else {
    first_cluster = ((uint32_t)fs_read_u16(root, offset + 20u) << 16u) |
                    (uint32_t)fs_read_u16(root, offset + 26u);
    file_size = fs_read_u32(root, offset + 28u);
    if (first_cluster < FS_CLUSTER_MIN_ALLOC || first_cluster > FS_CLUSTER_MAX_ALLOC) {
      return FS_ERR_STORAGE;
    }
  }

  result = fs_write_entry_content(first_cluster, content, content_len, append, &file_size);
  if (result != FS_OK) {
    return result;
  }

  fs_write_u32(root, offset + 28u, file_size);

  if (fs_store_fat(fat) != FS_OK) {
    return FS_ERR_STORAGE;
  }
  if (fs_write_sector(fs_cluster_to_lba(FS_ROOT_CLUSTER), root) != FS_OK) {
    return FS_ERR_STORAGE;
  }

  return FS_OK;
}
