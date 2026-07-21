/**
 * @file src/manifest_default.c
 * @brief In-OS minimal-manifest synthesiser implementation (#533 sub-slice
 *        of #409 / M7-TOOLCHAIN-006).
 *
 * Freestanding: no libc, no kernel/crypto headers. The in-OS `cc` driver
 * (#409) links this archive alongside `libsofpack.a` (#521) to emit a
 * companion `<binary>.manifest.json` next to its on-disk SOF output.
 *
 * Output shape (locked by tests/manifest_default_synthesise_test.c against
 * manifests/schema/v0.json):
 *
 *   {
 *     "manifest_version": 0,
 *     "os_abi_version": <abi_version>,
 *     "app": {
 *       "id": "<app_id>",
 *       "version": "<app_version>",
 *       "subject_id": <subject_id>,
 *       "binary": "<binary_path>"
 *     },
 *     "capabilities": {
 *       "request": []
 *     },
 *     "runtime": {
 *       "arena_bytes": 65536
 *     },
 *     "owner": {
 *       "kind": "<internal|external|local>"
 *     }
 *   }
 *
 * Whitespace: 2-space indent, '\n' line terminators, no trailing newline.
 * Two calls with identical params produce byte-identical bytes (pinned by
 * the host test's determinism arm).
 */

#include "../include/manifestgen/manifest_default.h"

/* ---- Local helpers ----------------------------------------------------- */
/* Freestanding sub-libc — sofpack.c uses the same minimal pattern. */

static size_t mg_strlen(const char *s) {
  size_t n = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

/* JSON string-value writer with conservative escaping. Backslash, quote,
 * and ASCII control bytes (<0x20) are escaped per RFC 8259; all other
 * bytes (including high-bit UTF-8 continuation bytes) pass through. The
 * v0 schema's `id` pattern (`^[a-z0-9][a-z0-9_-]{0,62}$`) and the
 * `binary`/`version` minLength=1 constraints don't admit control bytes,
 * but we escape defensively so a future schema relaxation can't silently
 * produce malformed JSON. */
static const char HEX[] = "0123456789abcdef";

typedef struct {
  char  *buf;
  size_t cap;
  size_t pos;
  int    overflow;
} mg_writer_t;

static void mg_emit_byte(mg_writer_t *w, char c) {
  if (w->overflow) {
    return;
  }
  /* +1 leaves room for the trailing NUL terminator. */
  if (w->pos + 1u >= w->cap) {
    w->overflow = 1;
    return;
  }
  w->buf[w->pos++] = c;
}

static void mg_emit_raw(mg_writer_t *w, const char *s) {
  size_t i;
  size_t n = mg_strlen(s);
  for (i = 0u; i < n; ++i) {
    mg_emit_byte(w, s[i]);
  }
}

static void mg_emit_escaped_string(mg_writer_t *w, const char *s) {
  size_t i;
  size_t n = mg_strlen(s);
  mg_emit_byte(w, '"');
  for (i = 0u; i < n; ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"') {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, '"');
    } else if (c == '\\') {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, '\\');
    } else if (c == '\n') {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, 'n');
    } else if (c == '\r') {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, 'r');
    } else if (c == '\t') {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, 't');
    } else if (c < 0x20u) {
      mg_emit_byte(w, '\\');
      mg_emit_byte(w, 'u');
      mg_emit_byte(w, '0');
      mg_emit_byte(w, '0');
      mg_emit_byte(w, HEX[(c >> 4) & 0xfu]);
      mg_emit_byte(w, HEX[c & 0xfu]);
    } else {
      mg_emit_byte(w, (char)c);
    }
  }
  mg_emit_byte(w, '"');
}

/* Emit a non-negative integer as decimal. Buffer is large enough for any
 * uint32_t (10 digits). */
static void mg_emit_u32(mg_writer_t *w, uint32_t v) {
  char tmp[11];
  int  i = 0;
  if (v == 0u) {
    mg_emit_byte(w, '0');
    return;
  }
  while (v > 0u && i < (int)sizeof(tmp)) {
    tmp[i++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (i > 0) {
    --i;
    mg_emit_byte(w, tmp[i]);
  }
}

static const char *mg_owner_kind_str(manifest_owner_kind_t k) {
  switch (k) {
    case MANIFEST_OWNER_KIND_INTERNAL: return "internal";
    case MANIFEST_OWNER_KIND_EXTERNAL: return "external";
    case MANIFEST_OWNER_KIND_LOCAL:    return "local";
    default:                           return 0;
  }
}

/* ---- Public API -------------------------------------------------------- */

manifest_default_result_t manifest_default_synthesise(
    const manifest_default_params_t *params,
    char                            *out_buf,
    size_t                           out_cap,
    size_t                          *out_len) {

  mg_writer_t w;
  const char *owner_str;

  if (params == 0 || out_buf == 0 || out_len == 0 || out_cap == 0u) {
    return MANIFEST_DEFAULT_ERR_INVALID_ARG;
  }
  if (params->app_id == 0 || params->app_version == 0 || params->binary_path == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_ARG;
  }
  if (mg_strlen(params->app_id) == 0u
      || mg_strlen(params->app_version) == 0u
      || mg_strlen(params->binary_path) == 0u) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }
  if (params->subject_id < 1u || params->subject_id > 7u) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }
  owner_str = mg_owner_kind_str(params->owner_kind);
  if (owner_str == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }

  w.buf      = out_buf;
  w.cap      = out_cap;
  w.pos      = 0u;
  w.overflow = 0;

  mg_emit_raw(&w, "{\n");
  mg_emit_raw(&w, "  \"manifest_version\": 0,\n");
  mg_emit_raw(&w, "  \"os_abi_version\": ");
  mg_emit_u32(&w, params->abi_version);
  mg_emit_raw(&w, ",\n");

  mg_emit_raw(&w, "  \"app\": {\n");
  mg_emit_raw(&w, "    \"id\": ");
  mg_emit_escaped_string(&w, params->app_id);
  mg_emit_raw(&w, ",\n");
  mg_emit_raw(&w, "    \"version\": ");
  mg_emit_escaped_string(&w, params->app_version);
  mg_emit_raw(&w, ",\n");
  mg_emit_raw(&w, "    \"subject_id\": ");
  mg_emit_u32(&w, (uint32_t)params->subject_id);
  mg_emit_raw(&w, ",\n");
  mg_emit_raw(&w, "    \"binary\": ");
  mg_emit_escaped_string(&w, params->binary_path);
  mg_emit_raw(&w, "\n");
  mg_emit_raw(&w, "  },\n");

  mg_emit_raw(&w, "  \"capabilities\": {\n");
  mg_emit_raw(&w, "    \"request\": []\n");
  mg_emit_raw(&w, "  },\n");

  mg_emit_raw(&w, "  \"runtime\": {\n");
  mg_emit_raw(&w, "    \"arena_bytes\": ");
  mg_emit_u32(&w, (uint32_t)MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES);
  mg_emit_raw(&w, "\n");
  mg_emit_raw(&w, "  },\n");

  mg_emit_raw(&w, "  \"owner\": {\n");
  mg_emit_raw(&w, "    \"kind\": ");
  mg_emit_escaped_string(&w, owner_str);
  mg_emit_raw(&w, "\n");
  mg_emit_raw(&w, "  }\n");

  mg_emit_raw(&w, "}\n");

  if (w.overflow) {
    return MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL;
  }
  /* Always NUL-terminate; capacity guarded by mg_emit_byte's `pos+1 >= cap`. */
  w.buf[w.pos] = '\0';
  *out_len = w.pos;
  return MANIFEST_DEFAULT_OK;
}
