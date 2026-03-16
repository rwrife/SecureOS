/**
 * @file sof.c
 * @brief SecureOS File Format (SOF) parsing, building, and verification.
 *
 * Purpose:
 *   Implements the SOF container format operations: parsing raw byte buffers
 *   into in-memory representations, constructing SOF files from build
 *   parameters, validating headers, looking up metadata entries, and
 *   providing signature verification stubs for future code-signing support.
 *
 * Interactions:
 *   - sof.h declares all types and function prototypes used here.
 *   - kernel/user/process.c calls sof_parse() to unwrap SOF containers
 *     and extract the embedded ELF payload before execution.
 *   - kernel/fs/fs_service.c calls sof_build() to wrap script-generated
 *     ELF binaries in SOF containers during filesystem initialization.
 *   - tools/sof_wrap/main.c calls sof_build() from the host-side CLI tool.
 *
 * Launched by:
 *   Not a standalone module; compiled into the kernel image and the
 *   sof_wrap host tool.
 */

#include "sof.h"

/* ---- Internal helpers -------------------------------------------------- */

static const uint8_t SOF_MAGIC[4] = {0x53, 0x45, 0x4F, 0x53}; /* "SEOS" */

static size_t sof_strlen(const char *s) {
  size_t len = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[len] != '\0') {
    ++len;
  }
  return len;
}

static void sof_memzero(uint8_t *dst, size_t size) {
  size_t i = 0u;
  for (i = 0u; i < size; ++i) {
    dst[i] = 0u;
  }
}

static void sof_memcpy(uint8_t *dst, const uint8_t *src, size_t size) {
  size_t i = 0u;
  for (i = 0u; i < size; ++i) {
    dst[i] = src[i];
  }
}

static uint16_t sof_read_u16(const uint8_t *buf, size_t off) {
  return (uint16_t)((uint16_t)buf[off] | ((uint16_t)buf[off + 1u] << 8u));
}

static uint32_t sof_read_u32(const uint8_t *buf, size_t off) {
  return (uint32_t)((uint32_t)buf[off] |
                    ((uint32_t)buf[off + 1u] << 8u) |
                    ((uint32_t)buf[off + 2u] << 16u) |
                    ((uint32_t)buf[off + 3u] << 24u));
}

static void sof_write_u16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off]      = (uint8_t)(val & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 8u) & 0xFFu);
}

static void sof_write_u32(uint8_t *buf, size_t off, uint32_t val) {
  buf[off]      = (uint8_t)(val & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((val >> 16u) & 0xFFu);
  buf[off + 3u] = (uint8_t)((val >> 24u) & 0xFFu);
}

static int sof_magic_matches(const uint8_t *data) {
  return data[0] == SOF_MAGIC[0] &&
         data[1] == SOF_MAGIC[1] &&
         data[2] == SOF_MAGIC[2] &&
         data[3] == SOF_MAGIC[3];
}

static void sof_decode_header(const uint8_t *data, sof_header_t *hdr) {
  hdr->magic[0]       = data[0];
  hdr->magic[1]       = data[1];
  hdr->magic[2]       = data[2];
  hdr->magic[3]       = data[3];
  hdr->format_version = data[4];
  hdr->file_type      = data[5];
  hdr->flags          = sof_read_u16(data, 6);
  hdr->total_size     = sof_read_u32(data, 8);
  hdr->meta_offset    = sof_read_u32(data, 12);
  hdr->meta_count     = sof_read_u16(data, 16);
  hdr->meta_size      = sof_read_u16(data, 18);
  hdr->payload_offset = sof_read_u32(data, 20);
  hdr->payload_size   = sof_read_u32(data, 24);
  hdr->sig_offset     = sof_read_u32(data, 28);
  hdr->sig_size       = sof_read_u32(data, 32);
}

/* ---- Public API -------------------------------------------------------- */

int sof_is_sof(const uint8_t *data, size_t data_len) {
  if (data == 0 || data_len < sizeof(sof_header_t)) {
    return 0;
  }
  return sof_magic_matches(data);
}

sof_result_t sof_validate_header(const uint8_t *data, size_t data_len) {
  sof_header_t hdr;

  if (data == 0 || data_len < sizeof(sof_header_t)) {
    return SOF_ERR_INVALID_SIZE;
  }

  if (!sof_magic_matches(data)) {
    return SOF_ERR_INVALID_MAGIC;
  }

  /* Decode header fields manually to avoid struct packing issues */
  hdr.format_version = data[4];
  hdr.file_type      = data[5];
  hdr.total_size     = sof_read_u32(data, 8);

  if (hdr.format_version != 1u) {
    return SOF_ERR_INVALID_VERSION;
  }

  if (hdr.file_type != SOF_TYPE_BIN &&
      hdr.file_type != SOF_TYPE_LIB &&
      hdr.file_type != SOF_TYPE_APP) {
    return SOF_ERR_INVALID_TYPE;
  }

  if (hdr.total_size > data_len) {
    return SOF_ERR_INVALID_SIZE;
  }

  return SOF_OK;
}

sof_result_t sof_parse(const uint8_t *data, size_t data_len, sof_parsed_file_t *out) {
  sof_result_t result = SOF_OK;
  size_t meta_cursor = 0u;
  size_t i = 0u;

  if (data == 0 || out == 0) {
    return SOF_ERR_INVALID_SIZE;
  }

  if (data_len < sizeof(sof_header_t)) {
    return SOF_ERR_INVALID_SIZE;
  }

  /* Validate header first */
  result = sof_validate_header(data, data_len);
  if (result != SOF_OK) {
    return result;
  }

  /* Decode full header from raw bytes */
  out->header.magic[0]      = data[0];
  out->header.magic[1]      = data[1];
  out->header.magic[2]      = data[2];
  out->header.magic[3]      = data[3];
  out->header.format_version = data[4];
  out->header.file_type     = data[5];
  out->header.flags         = sof_read_u16(data, 6);
  out->header.total_size    = sof_read_u32(data, 8);
  out->header.meta_offset   = sof_read_u32(data, 12);
  out->header.meta_count    = sof_read_u16(data, 16);
  out->header.meta_size     = sof_read_u16(data, 18);
  out->header.payload_offset = sof_read_u32(data, 20);
  out->header.payload_size  = sof_read_u32(data, 24);
  out->header.sig_offset    = sof_read_u32(data, 28);
  out->header.sig_size      = sof_read_u32(data, 32);

  /* Validate payload bounds */
  if (out->header.payload_offset + out->header.payload_size > data_len) {
    return SOF_ERR_INVALID_SIZE;
  }

  if (out->header.payload_size == 0u) {
    return SOF_ERR_NO_PAYLOAD;
  }

  /* Validate metadata bounds */
  if (out->header.meta_offset + out->header.meta_size > data_len) {
    return SOF_ERR_INVALID_SIZE;
  }

  /* Parse metadata TLV entries */
  out->meta_count = 0u;
  meta_cursor = out->header.meta_offset;

  for (i = 0u; i < out->header.meta_count && i < SOF_META_MAX_ENTRIES; ++i) {
    uint8_t key_id = 0u;
    uint8_t value_len = 0u;
    size_t copy_len = 0u;

    if (meta_cursor + 2u > data_len) {
      return SOF_ERR_INVALID_META;
    }

    key_id = data[meta_cursor];
    value_len = data[meta_cursor + 1u];
    meta_cursor += 2u;

    if (meta_cursor + value_len > data_len) {
      return SOF_ERR_INVALID_META;
    }

    out->meta[i].key = (sof_meta_key_t)key_id;
    out->meta[i].value_len = value_len;

    copy_len = value_len;
    if (copy_len >= SOF_META_VALUE_MAX) {
      copy_len = SOF_META_VALUE_MAX - 1u;
    }

    {
      size_t j = 0u;
      for (j = 0u; j < copy_len; ++j) {
        out->meta[i].value[j] = (char)data[meta_cursor + j];
      }
      out->meta[i].value[copy_len] = '\0';
    }

    meta_cursor += value_len;
    out->meta_count = i + 1u;
  }

  /* Set payload pointer */
  out->payload = &data[out->header.payload_offset];
  out->payload_size = out->header.payload_size;

  /* Signature status */
  out->has_signature = (out->header.sig_offset != 0u && out->header.sig_size != 0u) ? 1 : 0;

  return SOF_OK;
}

sof_result_t sof_get_meta(const sof_parsed_file_t *parsed,
                           sof_meta_key_t key,
                           const char **out_value,
                           size_t *out_len) {
  size_t i = 0u;

  if (parsed == 0 || out_value == 0 || out_len == 0) {
    return SOF_ERR_INVALID_META;
  }

  for (i = 0u; i < parsed->meta_count; ++i) {
    if (parsed->meta[i].key == key) {
      *out_value = parsed->meta[i].value;
      *out_len = (size_t)parsed->meta[i].value_len;
      return SOF_OK;
    }
  }

  return SOF_ERR_INVALID_META;
}

/**
 * Internal helper: append a single TLV metadata entry to the buffer.
 * Returns the number of bytes written (2 + value_len), or 0 on error.
 */
static size_t sof_write_meta_entry(uint8_t *buf,
                                    size_t buf_size,
                                    size_t cursor,
                                    uint8_t key_id,
                                    const char *value) {
  size_t value_len = 0u;
  size_t i = 0u;

  if (value == 0) {
    return 0u;
  }

  value_len = sof_strlen(value);
  if (value_len > 255u) {
    value_len = 255u;
  }

  if (cursor + 2u + value_len > buf_size) {
    return 0u;
  }

  buf[cursor] = key_id;
  buf[cursor + 1u] = (uint8_t)value_len;

  for (i = 0u; i < value_len; ++i) {
    buf[cursor + 2u + i] = (uint8_t)value[i];
  }

  return 2u + value_len;
}

sof_result_t sof_build(const sof_build_params_t *params,
                        uint8_t *out_buffer,
                        size_t out_buffer_size,
                        size_t *out_total_size) {
  size_t header_size = sizeof(sof_header_t);
  size_t meta_cursor = 0u;
  size_t meta_start = header_size;
  size_t meta_written = 0u;
  uint16_t meta_count = 0u;
  size_t payload_start = 0u;
  size_t total_size = 0u;
  size_t written = 0u;

  if (params == 0 || out_buffer == 0 || out_total_size == 0) {
    return SOF_ERR_INVALID_SIZE;
  }

  if (params->elf_payload == 0 || params->elf_payload_size == 0u) {
    return SOF_ERR_NO_PAYLOAD;
  }

  if (params->file_type != SOF_TYPE_BIN && params->file_type != SOF_TYPE_LIB) {
    return SOF_ERR_INVALID_TYPE;
  }

  /* First pass: calculate metadata size */
  meta_cursor = meta_start;

  /* Write metadata entries into buffer (we'll calculate size either way) */
  /* We need to know total size first, so do a dry-run for size calculation */
  {
    size_t dry_meta_size = 0u;

    if (params->name != 0) {
      dry_meta_size += 2u + sof_strlen(params->name);
      ++meta_count;
    }
    if (params->description != 0) {
      dry_meta_size += 2u + sof_strlen(params->description);
      ++meta_count;
    }
    if (params->author != 0) {
      dry_meta_size += 2u + sof_strlen(params->author);
      ++meta_count;
    }
    if (params->version != 0) {
      dry_meta_size += 2u + sof_strlen(params->version);
      ++meta_count;
    }
    if (params->date != 0) {
      dry_meta_size += 2u + sof_strlen(params->date);
      ++meta_count;
    }
    if (params->icon != 0) {
      dry_meta_size += 2u + sof_strlen(params->icon);
      ++meta_count;
    }

    meta_written = dry_meta_size;
    payload_start = meta_start + meta_written;
    total_size = payload_start + params->elf_payload_size;
  }

  if (total_size > out_buffer_size) {
    return SOF_ERR_BUFFER_TOO_SMALL;
  }

  /* Zero the entire output buffer */
  sof_memzero(out_buffer, total_size);

  /* Write header */
  out_buffer[0] = SOF_MAGIC[0];
  out_buffer[1] = SOF_MAGIC[1];
  out_buffer[2] = SOF_MAGIC[2];
  out_buffer[3] = SOF_MAGIC[3];
  out_buffer[4] = 1u; /* format_version */
  out_buffer[5] = (uint8_t)params->file_type;
  sof_write_u16(out_buffer, 6, 0u); /* flags */
  sof_write_u32(out_buffer, 8, (uint32_t)total_size);
  sof_write_u32(out_buffer, 12, (uint32_t)meta_start); /* meta_offset */
  sof_write_u16(out_buffer, 16, meta_count);            /* meta_count */
  sof_write_u16(out_buffer, 18, (uint16_t)meta_written); /* meta_size */
  sof_write_u32(out_buffer, 20, (uint32_t)payload_start); /* payload_offset */
  sof_write_u32(out_buffer, 24, (uint32_t)params->elf_payload_size); /* payload_size */
  sof_write_u32(out_buffer, 28, 0u); /* sig_offset = 0 (unsigned) */
  sof_write_u32(out_buffer, 32, 0u); /* sig_size = 0 (unsigned) */

  /* Write metadata TLV entries */
  meta_cursor = meta_start;

  if (params->name != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_NAME, params->name);
    meta_cursor += written;
  }
  if (params->description != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_DESCRIPTION, params->description);
    meta_cursor += written;
  }
  if (params->author != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_AUTHOR, params->author);
    meta_cursor += written;
  }
  if (params->version != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_VERSION, params->version);
    meta_cursor += written;
  }
  if (params->date != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_DATE, params->date);
    meta_cursor += written;
  }
  if (params->icon != 0) {
    written = sof_write_meta_entry(out_buffer, out_buffer_size, meta_cursor,
                                    (uint8_t)SOF_META_ICON, params->icon);
    meta_cursor += written;
  }

  /* Write ELF payload */
  sof_memcpy(&out_buffer[payload_start], params->elf_payload, params->elf_payload_size);

  *out_total_size = total_size;
  return SOF_OK;
}

int sof_signature_present(const sof_header_t *header) {
  if (header == 0) {
    return 0;
  }
  return (header->sig_offset != 0u && header->sig_size != 0u) ? 1 : 0;
}

sof_result_t sof_verify_signature(const uint8_t *data,
                                   size_t data_len,
                                   const sof_parsed_file_t *parsed) {
  /* Stub: always returns SOF_OK.
   * Future: verify digital signature against payload hash using the
   * algorithm specified in the signature metadata entries. */
  (void)data;
  (void)data_len;
  (void)parsed;
  return SOF_OK;
}

sof_result_t sof_parse_app_bundle(const uint8_t *data,
                                   size_t data_len,
                                   sof_parsed_file_t *out) {
  sof_result_t result = SOF_OK;

  if (data == 0 || out == 0 || data_len < sizeof(sof_header_t)) {
    return SOF_ERR_INVALID_SIZE;
  }

  result = sof_validate_header(data, data_len);
  if (result != SOF_OK) {
    return result;
  }

  /* Check that this is actually an APP bundle */
  if (data[5] != SOF_TYPE_APP) {
    return SOF_ERR_INVALID_TYPE;
  }

  /* Bundles are not yet supported */
  return SOF_ERR_INVALID_TYPE;
}