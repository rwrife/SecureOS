/**
 * @file soflib.h
 * @brief User-space library for reading SecureOS File Format (SOF) metadata.
 *
 * Purpose:
 *   Provides a lightweight, header-only API for extracting SOF container
 *   metadata (name, description, author, version, date, file type,
 *   signature status) from raw file bytes.  Mirrors the kernel-side SOF
 *   definitions so user-space applications can inspect .bin and .lib files
 *   without depending on internal kernel headers.
 *
 * Interactions:
 *   - User applications include this header to inspect SOF file metadata.
 *   - The about OS command uses this library conceptually; the actual
 *     kernel-side implementation reads files and parses SOF directly.
 *   - Follows the same static-inline pattern as envlib.h and fslib.h.
 *
 * Launched by:
 *   Header-only; included by user/apps/os/about/main.c and any other
 *   user-space code that needs SOF metadata inspection.
 */

#ifndef SECUREOS_SOFLIB_H
#define SECUREOS_SOFLIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int soflib_handle_t;

enum {
  SOFLIB_HANDLE_INVALID = 0u,
  SOFLIB_META_VALUE_MAX = 64u,
  SOFLIB_META_MAX_ENTRIES = 12u,
  SOFLIB_HEADER_SIZE = 36u,
};

typedef enum {
  SOFLIB_STATUS_OK = 0,
  SOFLIB_STATUS_INVALID = 1,
  SOFLIB_STATUS_NOT_FOUND = 2,
  SOFLIB_STATUS_ERROR = 3,
} soflib_status_t;

typedef enum {
  SOFLIB_TYPE_INVALID = 0x00,
  SOFLIB_TYPE_BIN     = 0x01,
  SOFLIB_TYPE_LIB     = 0x02,
  SOFLIB_TYPE_APP     = 0x03,
} soflib_file_type_t;

typedef enum {
  SOFLIB_META_NAME        = 0x01,
  SOFLIB_META_DESCRIPTION = 0x02,
  SOFLIB_META_AUTHOR      = 0x03,
  SOFLIB_META_VERSION     = 0x04,
  SOFLIB_META_DATE        = 0x05,
  SOFLIB_META_ICON        = 0x06,
} soflib_meta_key_t;

typedef struct {
  soflib_meta_key_t key;
  unsigned char value_len;
  char value[SOFLIB_META_VALUE_MAX];
} soflib_meta_entry_t;

typedef struct {
  unsigned char format_version;
  soflib_file_type_t file_type;
  unsigned int total_size;
  unsigned int payload_size;
  int has_signature;
  soflib_meta_entry_t meta[SOFLIB_META_MAX_ENTRIES];
  unsigned int meta_count;
} soflib_parsed_t;

/* ---- Internal helpers -------------------------------------------------- */

static inline unsigned int soflib_read_u16(const unsigned char *buf, unsigned int off) {
  return (unsigned int)buf[off] | ((unsigned int)buf[off + 1u] << 8u);
}

static inline unsigned int soflib_read_u32(const unsigned char *buf, unsigned int off) {
  return (unsigned int)buf[off] |
         ((unsigned int)buf[off + 1u] << 8u) |
         ((unsigned int)buf[off + 2u] << 16u) |
         ((unsigned int)buf[off + 3u] << 24u);
}

/* ---- Public API -------------------------------------------------------- */

/**
 * Check if raw bytes represent a SOF file (magic = "SEOS").
 * Returns 1 if valid SOF header detected, 0 otherwise.
 */
static inline int soflib_is_sof(const unsigned char *data, unsigned int data_len) {
  if (data == 0 || data_len < SOFLIB_HEADER_SIZE) {
    return 0;
  }
  return (data[0] == 0x53 && data[1] == 0x45 &&
          data[2] == 0x4F && data[3] == 0x53);
}

/**
 * Parse raw SOF file bytes into a soflib_parsed_t structure.
 * Extracts header fields and all TLV metadata entries.
 */
static inline soflib_status_t soflib_parse(soflib_handle_t handle,
                                            const unsigned char *data,
                                            unsigned int data_len,
                                            soflib_parsed_t *out) {
  unsigned int meta_offset = 0u;
  unsigned int meta_count = 0u;
  unsigned int meta_cursor = 0u;
  unsigned int i = 0u;

  (void)handle;

  if (data == 0 || out == 0 || data_len < SOFLIB_HEADER_SIZE) {
    return SOFLIB_STATUS_INVALID;
  }

  if (!soflib_is_sof(data, data_len)) {
    return SOFLIB_STATUS_INVALID;
  }

  out->format_version = data[4];
  out->file_type = (soflib_file_type_t)data[5];
  out->total_size = soflib_read_u32(data, 8);
  out->payload_size = soflib_read_u32(data, 24);
  out->has_signature = (soflib_read_u32(data, 28) != 0u &&
                        soflib_read_u32(data, 32) != 0u) ? 1 : 0;

  if (out->total_size > data_len) {
    return SOFLIB_STATUS_INVALID;
  }

  meta_offset = soflib_read_u32(data, 12);
  meta_count = soflib_read_u16(data, 16);
  out->meta_count = 0u;

  meta_cursor = meta_offset;
  for (i = 0u; i < meta_count && i < SOFLIB_META_MAX_ENTRIES; ++i) {
    unsigned char key_id = 0u;
    unsigned char value_len = 0u;
    unsigned int copy_len = 0u;
    unsigned int j = 0u;

    if (meta_cursor + 2u > data_len) {
      break;
    }

    key_id = data[meta_cursor];
    value_len = data[meta_cursor + 1u];
    meta_cursor += 2u;

    if (meta_cursor + value_len > data_len) {
      break;
    }

    out->meta[i].key = (soflib_meta_key_t)key_id;
    out->meta[i].value_len = value_len;

    copy_len = value_len;
    if (copy_len >= SOFLIB_META_VALUE_MAX) {
      copy_len = SOFLIB_META_VALUE_MAX - 1u;
    }

    for (j = 0u; j < copy_len; ++j) {
      out->meta[i].value[j] = (char)data[meta_cursor + j];
    }
    out->meta[i].value[copy_len] = '\0';

    meta_cursor += value_len;
    out->meta_count = i + 1u;
  }

  return SOFLIB_STATUS_OK;
}

/**
 * Look up a specific metadata entry by key from a parsed SOF structure.
 * Copies the value into out_value (null-terminated).
 * Returns SOFLIB_STATUS_OK on success, SOFLIB_STATUS_NOT_FOUND if the
 * key is not present.
 */
static inline soflib_status_t soflib_get_meta(soflib_handle_t handle,
                                               const soflib_parsed_t *parsed,
                                               soflib_meta_key_t key,
                                               char *out_value,
                                               unsigned int out_value_size) {
  unsigned int i = 0u;
  unsigned int j = 0u;
  unsigned int copy_len = 0u;

  (void)handle;

  if (parsed == 0 || out_value == 0 || out_value_size == 0u) {
    return SOFLIB_STATUS_ERROR;
  }

  out_value[0] = '\0';
  for (i = 0u; i < parsed->meta_count; ++i) {
    if ((unsigned int)parsed->meta[i].key == (unsigned int)key) {
      copy_len = (unsigned int)parsed->meta[i].value_len;
      if (copy_len >= out_value_size) {
        copy_len = out_value_size - 1u;
      }
      for (j = 0u; j < copy_len; ++j) {
        out_value[j] = parsed->meta[i].value[j];
      }
      out_value[copy_len] = '\0';
      return SOFLIB_STATUS_OK;
    }
  }

  return SOFLIB_STATUS_NOT_FOUND;
}

/**
 * Return a human-readable string for the file type.
 */
static inline const char *soflib_type_name(soflib_file_type_t file_type) {
  switch (file_type) {
    case SOFLIB_TYPE_BIN: return "binary";
    case SOFLIB_TYPE_LIB: return "library";
    case SOFLIB_TYPE_APP: return "application";
    default:              return "unknown";
  }
}

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_SOFLIB_H */