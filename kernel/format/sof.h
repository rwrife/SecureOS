/**
 * @file sof.h
 * @brief SecureOS File Format (SOF) type definitions and function prototypes.
 *
 * Purpose:
 *   Defines the SOF container format — a binary header + TLV metadata + ELF
 *   payload wrapper used for all executable (.bin) and library (.lib) files
 *   in SecureOS.  Includes enums for file types, signature algorithms, and
 *   metadata keys; packed on-disk header struct; in-memory parsed
 *   representations; and build/parse/verify function prototypes.
 *
 * Interactions:
 *   - kernel/format/sof.c implements the parsing, building, and verification
 *     functions declared here.
 *   - kernel/user/process.c includes this header to parse SOF containers
 *     before extracting the ELF payload for execution.
 *   - kernel/fs/fs_service.c includes this header to build SOF containers
 *     around script-generated ELF binaries at filesystem init time.
 *   - tools/sof_wrap/main.c includes this header for the host-side CLI
 *     wrapping tool.
 *
 * Launched by:
 *   Header-only; not compiled standalone.  Included by the files listed above.
 */

#ifndef SECUREOS_SOF_H
#define SECUREOS_SOF_H

#include <stddef.h>
#include <stdint.h>

/* ---- File type enum ---------------------------------------------------- */

typedef enum {
  SOF_TYPE_INVALID = 0x00, /* Not a valid SOF file */
  SOF_TYPE_BIN     = 0x01, /* Executable binary (.bin) */
  SOF_TYPE_LIB     = 0x02, /* Library (.lib) */
  SOF_TYPE_APP     = 0x03, /* Application bundle (reserved, not yet implemented) */
} sof_file_type_t;

/* ---- Signature algorithm enum (future) --------------------------------- */

typedef enum {
  SOF_SIG_NONE    = 0x00, /* Unsigned */
  SOF_SIG_ED25519 = 0x01, /* Reserved for future */
  SOF_SIG_RSA2048 = 0x02, /* Reserved for future */
} sof_sig_algorithm_t;

/* ---- Metadata TLV key identifiers ------------------------------------- */

typedef enum {
  SOF_META_NAME        = 0x01,
  SOF_META_DESCRIPTION = 0x02,
  SOF_META_AUTHOR      = 0x03,
  SOF_META_VERSION     = 0x04,
  SOF_META_DATE        = 0x05,
  SOF_META_ICON        = 0x06,
  SOF_META_SIG_ALGO    = 0x10, /* Future: signature algorithm name */
  SOF_META_SIG_KEYID   = 0x11, /* Future: signing key identifier */
  SOF_META_SIG_HASH    = 0x12, /* Future: payload hash algorithm */
} sof_meta_key_t;

/* ---- On-disk header (32 bytes, packed, little-endian) ------------------ */

typedef struct {
  uint8_t  magic[4];       /* "SEOS" = {0x53, 0x45, 0x4F, 0x53} */
  uint8_t  format_version; /* 1 */
  uint8_t  file_type;      /* sof_file_type_t */
  uint16_t flags;          /* Reserved, 0 */
  uint32_t total_size;     /* Total file size in bytes */
  uint32_t meta_offset;    /* Byte offset to metadata section */
  uint16_t meta_count;     /* Number of metadata TLV entries */
  uint16_t meta_size;      /* Total size of metadata section in bytes */
  uint32_t payload_offset; /* Byte offset to ELF payload */
  uint32_t payload_size;   /* Size of ELF payload in bytes */
  uint32_t sig_offset;     /* Byte offset to signature (0 = unsigned) */
  uint32_t sig_size;       /* Size of signature (0 = unsigned) */
} __attribute__((packed)) sof_header_t;

/* Compile-time check: header must be exactly 36 bytes */
_Static_assert(sizeof(sof_header_t) == 36, "sof_header_t must be 36 bytes");

/* ---- In-memory metadata entry ------------------------------------------ */

enum { SOF_META_VALUE_MAX = 64 };

typedef struct {
  sof_meta_key_t key;
  uint8_t value_len;
  char value[SOF_META_VALUE_MAX]; /* null-terminated copy */
} sof_meta_entry_t;

/* ---- Parse result codes ------------------------------------------------ */

typedef enum {
  SOF_OK                    = 0,
  SOF_ERR_INVALID_MAGIC     = 1,
  SOF_ERR_INVALID_VERSION   = 2,
  SOF_ERR_INVALID_TYPE      = 3,
  SOF_ERR_INVALID_SIZE      = 4,
  SOF_ERR_INVALID_META      = 5,
  SOF_ERR_BUFFER_TOO_SMALL  = 6,
  SOF_ERR_NO_PAYLOAD        = 7,
  SOF_ERR_SIGNATURE_REQUIRED = 8, /* Future: unsigned file rejected */
} sof_result_t;

/* ---- Parsed SOF file (in-memory) --------------------------------------- */

enum { SOF_META_MAX_ENTRIES = 12 };

typedef struct {
  sof_header_t header;
  sof_meta_entry_t meta[SOF_META_MAX_ENTRIES];
  size_t meta_count;
  const uint8_t *payload;  /* Pointer into source buffer */
  size_t payload_size;
  int has_signature;       /* 0 = unsigned, 1 = signed (future) */
} sof_parsed_file_t;

/* ---- Build parameters -------------------------------------------------- */

typedef struct {
  sof_file_type_t file_type;
  const char *name;
  const char *description;
  const char *author;
  const char *version;
  const char *date;
  const char *icon; /* May be NULL */
  const uint8_t *elf_payload;
  size_t elf_payload_size;
} sof_build_params_t;

/* ---- Reserved .app bundle header (STUB) -------------------------------- */

typedef struct {
  uint32_t entry_count;       /* Number of entries in the bundle (future) */
  uint32_t manifest_offset;   /* Offset to bundle manifest (future) */
  uint32_t manifest_size;     /* Size of bundle manifest (future) */
  uint32_t compression_algo;  /* 0=NONE, future: 1=LZ4, 2=ZSTD */
  uint32_t reserved[4];       /* Reserved for future use */
} sof_app_bundle_header_t;

/* ---- Function prototypes ----------------------------------------------- */

/**
 * Parse a raw byte buffer into a sof_parsed_file_t.
 * Validates magic, version, type, size fields.  Extracts all metadata TLV
 * entries.  Sets out->payload to point into the source buffer at payload_offset.
 */
sof_result_t sof_parse(const uint8_t *data, size_t data_len, sof_parsed_file_t *out);

/**
 * Quick validation of just the 32-byte header without full metadata parsing.
 */
sof_result_t sof_validate_header(const uint8_t *data, size_t data_len);

/**
 * Returns 1 if the first 4 bytes match "SEOS" magic and data_len >= 32,
 * 0 otherwise.
 */
int sof_is_sof(const uint8_t *data, size_t data_len);

/**
 * Look up a metadata entry by key.  Returns pointer to the null-terminated
 * value string in the parsed struct.  Returns SOF_ERR_INVALID_META if not found.
 */
sof_result_t sof_get_meta(const sof_parsed_file_t *parsed,
                           sof_meta_key_t key,
                           const char **out_value,
                           size_t *out_len);

/**
 * Construct a complete SOF file.  Writes header, metadata TLV entries, and
 * payload into out_buffer.  Sets signature fields to 0.
 * Returns SOF_ERR_BUFFER_TOO_SMALL if the buffer is insufficient.
 */
sof_result_t sof_build(const sof_build_params_t *params,
                        uint8_t *out_buffer,
                        size_t out_buffer_size,
                        size_t *out_total_size);

/**
 * Returns 1 if sig_offset != 0 && sig_size != 0, 0 otherwise.
 * Currently always returns 0 for newly built files.
 */
int sof_signature_present(const sof_header_t *header);

/**
 * Stub — always returns SOF_OK.
 * Future: verify digital signature against payload hash.
 */
sof_result_t sof_verify_signature(const uint8_t *data,
                                   size_t data_len,
                                   const sof_parsed_file_t *parsed);

/**
 * STUB — Reserved for future .app bundle parsing.
 * Validates that the SOF header has file_type == SOF_TYPE_APP, then returns
 * SOF_ERR_INVALID_TYPE to indicate bundles are not yet supported.
 */
sof_result_t sof_parse_app_bundle(const uint8_t *data,
                                   size_t data_len,
                                   sof_parsed_file_t *out);

#endif /* SECUREOS_SOF_H */