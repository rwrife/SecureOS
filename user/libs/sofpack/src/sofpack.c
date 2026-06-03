/**
 * @file src/sofpack.c
 * @brief Userland SOF container packer implementation (issue #409 sub-slice).
 *
 * Purpose:
 *   Freestanding implementation of `sofpack_wrap()` — the userland-callable
 *   factoring of `sof_build()` from kernel/format/sof.c. Produces
 *   byte-identical SOF containers (unsigned path) without depending on
 *   any kernel-internal headers, so the in-OS `cc` driver (#409) can link
 *   it without dragging crypto/kernel headers into userland.
 *
 *   See sofpack.h for the public contract; tests/sofpack_wrap_test.c
 *   pins the byte-for-byte equivalence with sof_build() / sof_parse().
 *
 * Wire layout (all little-endian, matches kernel/format/sof.c sof_build):
 *
 *   offset  size  field
 *     0      4    magic "SEOS"
 *     4      1    format_version = 1
 *     5      1    file_type (BIN=1, LIB=2)
 *     6      2    flags = 0
 *     8      4    total_size
 *    12      4    meta_offset = 36 (sizeof header)
 *    16      2    meta_count
 *    18      2    meta_size
 *    20      4    payload_offset
 *    24      4    payload_size
 *    28      4    sig_offset = 0 (unsigned)
 *    32      4    sig_size   = 0 (unsigned)
 *    36     ...   TLV metadata, then ELF payload
 *
 * Launched by:
 *   Compiled into `libsofpack.a` (built by the host test driver today;
 *   the on-target build wires it into the `cc` app per plan Phase 5).
 */

#include "../include/sofpack/sofpack.h"

/* Meta-key wire identifiers — must match the sof_meta_key_t values in
 * kernel/format/sof.h. Duplicating here so userland callers do not pull
 * in kernel headers; the host test pins both sides agree. */
enum {
  SOFPACK_META_NAME        = 0x01,
  SOFPACK_META_DESCRIPTION = 0x02,
  SOFPACK_META_AUTHOR      = 0x03,
  SOFPACK_META_VERSION     = 0x04,
  SOFPACK_META_DATE        = 0x05,
  SOFPACK_META_ICON        = 0x06,
  SOFPACK_META_SYSCALL_ID  = 0x20,
};

enum {
  SOFPACK_HEADER_SIZE = 36u,
  SOFPACK_META_VALUE_MAX_WIRE = 255u, /* TLV length is a single byte */
};

static size_t sofpack_strlen(const char *s) {
  size_t n = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static void sofpack_memzero(uint8_t *dst, size_t n) {
  size_t i;
  for (i = 0u; i < n; ++i) {
    dst[i] = 0u;
  }
}

static void sofpack_memcpy(uint8_t *dst, const uint8_t *src, size_t n) {
  size_t i;
  for (i = 0u; i < n; ++i) {
    dst[i] = src[i];
  }
}

static void sofpack_write_u16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off]      = (uint8_t)(val & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 8u) & 0xFFu);
}

static void sofpack_write_u32(uint8_t *buf, size_t off, uint32_t val) {
  buf[off]      = (uint8_t)(val & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((val >> 16u) & 0xFFu);
  buf[off + 3u] = (uint8_t)((val >> 24u) & 0xFFu);
}

/* Returns the on-wire length (2 + value_len) for a TLV with the given
 * value, or 0 if value is NULL. Mirrors the implicit "skip if NULL"
 * behaviour of sof_build (kernel/format/sof.c). */
static size_t sofpack_tlv_len(const char *value, size_t *out_value_len) {
  size_t vlen;
  if (value == 0) {
    if (out_value_len != 0) {
      *out_value_len = 0u;
    }
    return 0u;
  }
  vlen = sofpack_strlen(value);
  if (vlen > SOFPACK_META_VALUE_MAX_WIRE) {
    vlen = SOFPACK_META_VALUE_MAX_WIRE;
  }
  if (out_value_len != 0) {
    *out_value_len = vlen;
  }
  return 2u + vlen;
}

static size_t sofpack_write_tlv(uint8_t *buf,
                                size_t cursor,
                                uint8_t key,
                                const char *value) {
  size_t vlen;
  size_t i;
  if (value == 0) {
    return 0u;
  }
  vlen = sofpack_strlen(value);
  if (vlen > SOFPACK_META_VALUE_MAX_WIRE) {
    vlen = SOFPACK_META_VALUE_MAX_WIRE;
  }
  buf[cursor]      = key;
  buf[cursor + 1u] = (uint8_t)vlen;
  for (i = 0u; i < vlen; ++i) {
    buf[cursor + 2u + i] = (uint8_t)value[i];
  }
  return 2u + vlen;
}

/* Compute the byte budget (header + meta + payload) and meta_count.
 * Shared by sofpack_wrap_size and sofpack_wrap so both stay in sync. */
static sofpack_result_t sofpack_layout(const sofpack_build_params_t *params,
                                       size_t *out_total,
                                       size_t *out_meta_size,
                                       uint16_t *out_meta_count) {
  size_t meta_size = 0u;
  uint16_t meta_count = 0u;
  size_t add;

  if (params == 0 || out_total == 0) {
    return SOFPACK_ERR_INVALID_ARG;
  }
  if (params->elf_payload == 0 || params->elf_payload_size == 0u) {
    return SOFPACK_ERR_NO_PAYLOAD;
  }
  if (params->file_type != SOFPACK_TYPE_BIN &&
      params->file_type != SOFPACK_TYPE_LIB) {
    return SOFPACK_ERR_INVALID_TYPE;
  }

  add = sofpack_tlv_len(params->name, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->description, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->author, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->version, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->date, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->icon, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }
  add = sofpack_tlv_len(params->syscall_id, 0);
  if (add > 0u) { meta_size += add; ++meta_count; }

  *out_total = SOFPACK_HEADER_SIZE + meta_size + params->elf_payload_size;
  if (out_meta_size != 0) {
    *out_meta_size = meta_size;
  }
  if (out_meta_count != 0) {
    *out_meta_count = meta_count;
  }
  return SOFPACK_OK;
}

sofpack_result_t sofpack_wrap_size(const sofpack_build_params_t *params,
                                   size_t *out_size) {
  size_t total = 0u;
  sofpack_result_t rc;
  if (out_size == 0) {
    return SOFPACK_ERR_INVALID_ARG;
  }
  rc = sofpack_layout(params, &total, 0, 0);
  if (rc != SOFPACK_OK) {
    return rc;
  }
  *out_size = total;
  return SOFPACK_OK;
}

sofpack_result_t sofpack_wrap(const sofpack_build_params_t *params,
                              uint8_t *out_buffer,
                              size_t out_buffer_size,
                              size_t *out_size) {
  size_t total = 0u;
  size_t meta_size = 0u;
  uint16_t meta_count = 0u;
  size_t cursor;
  size_t written;
  size_t payload_off;
  sofpack_result_t rc;

  if (out_buffer == 0 || out_size == 0) {
    return SOFPACK_ERR_INVALID_ARG;
  }

  rc = sofpack_layout(params, &total, &meta_size, &meta_count);
  if (rc != SOFPACK_OK) {
    return rc;
  }

  if (total > out_buffer_size) {
    return SOFPACK_ERR_BUFFER_TOO_SMALL;
  }

  sofpack_memzero(out_buffer, total);

  /* Header. */
  out_buffer[0] = (uint8_t)'S';
  out_buffer[1] = (uint8_t)'E';
  out_buffer[2] = (uint8_t)'O';
  out_buffer[3] = (uint8_t)'S';
  out_buffer[4] = 1u; /* format_version */
  out_buffer[5] = (uint8_t)params->file_type;
  sofpack_write_u16(out_buffer, 6, 0u); /* flags */
  sofpack_write_u32(out_buffer, 8, (uint32_t)total);
  sofpack_write_u32(out_buffer, 12, (uint32_t)SOFPACK_HEADER_SIZE); /* meta_offset */
  sofpack_write_u16(out_buffer, 16, meta_count);
  sofpack_write_u16(out_buffer, 18, (uint16_t)meta_size);
  payload_off = SOFPACK_HEADER_SIZE + meta_size;
  sofpack_write_u32(out_buffer, 20, (uint32_t)payload_off);
  sofpack_write_u32(out_buffer, 24, (uint32_t)params->elf_payload_size);
  sofpack_write_u32(out_buffer, 28, 0u); /* sig_offset (unsigned) */
  sofpack_write_u32(out_buffer, 32, 0u); /* sig_size   (unsigned) */

  /* TLV metadata — ordering MUST match sof_build (kernel/format/sof.c)
   * so byte-equivalence holds. */
  cursor = SOFPACK_HEADER_SIZE;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_NAME, params->name);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_DESCRIPTION, params->description);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_AUTHOR, params->author);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_VERSION, params->version);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_DATE, params->date);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_ICON, params->icon);
  cursor += written;
  written = sofpack_write_tlv(out_buffer, cursor, SOFPACK_META_SYSCALL_ID, params->syscall_id);
  cursor += written;

  /* ELF payload, byte-for-byte. */
  sofpack_memcpy(&out_buffer[payload_off],
                 params->elf_payload,
                 params->elf_payload_size);

  *out_size = total;
  return SOFPACK_OK;
}
