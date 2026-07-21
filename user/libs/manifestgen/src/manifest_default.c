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

const char *manifest_default_owner_kind_tag(manifest_owner_kind_t k) {
  switch (k) {
    case MANIFEST_OWNER_KIND_INTERNAL: return "internal";
    case MANIFEST_OWNER_KIND_EXTERNAL: return "external";
    case MANIFEST_OWNER_KIND_LOCAL:    return "local";
    default:                           return 0;
  }
}

static int mg_is_lower_hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static manifest_default_result_t mg_append_byte(
    char *out_buf,
    size_t out_cap,
    size_t *pos,
    char c) {
  if (*pos + 1u >= out_cap) {
    return MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL;
  }
  out_buf[*pos] = c;
  *pos += 1u;
  return MANIFEST_DEFAULT_OK;
}

static manifest_default_result_t mg_append_str(
    char *out_buf,
    size_t out_cap,
    size_t *pos,
    const char *src) {
  size_t i;
  size_t n;
  if (src == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_ARG;
  }
  n = mg_strlen(src);
  for (i = 0u; i < n; ++i) {
    manifest_default_result_t st = mg_append_byte(out_buf, out_cap, pos, src[i]);
    if (st != MANIFEST_DEFAULT_OK) {
      return st;
    }
  }
  return MANIFEST_DEFAULT_OK;
}

static manifest_default_result_t mg_append_u32(
    char *out_buf,
    size_t out_cap,
    size_t *pos,
    uint32_t v) {
  char tmp[11];
  int i = 0;
  if (v == 0u) {
    return mg_append_byte(out_buf, out_cap, pos, '0');
  }
  while (v > 0u && i < (int)sizeof(tmp)) {
    tmp[i++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (i > 0) {
    manifest_default_result_t st;
    --i;
    st = mg_append_byte(out_buf, out_cap, pos, tmp[i]);
    if (st != MANIFEST_DEFAULT_OK) {
      return st;
    }
  }
  return MANIFEST_DEFAULT_OK;
}

/* ---- Public API -------------------------------------------------------- */

manifest_default_result_t manifest_default_synthesise(
    const manifest_default_params_t *params,
    char                            *out_buf,
    size_t                           out_cap,
    size_t                          *out_len) {

  mg_writer_t w;
  const char *owner_str;
  uint32_t runtime_arena_bytes;

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
  owner_str = manifest_default_owner_kind_tag(params->owner_kind);
  if (owner_str == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }
  runtime_arena_bytes = params->runtime_arena_bytes;
  if (runtime_arena_bytes == 0u) {
    runtime_arena_bytes = (uint32_t)MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES;
  } else if (runtime_arena_bytes < (uint32_t)MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES
             || runtime_arena_bytes > (uint32_t)MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX) {
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
  mg_emit_u32(&w, runtime_arena_bytes);
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

const char *manifest_default_audit_fail_reason(
    manifest_default_result_t        rc,
    const manifest_default_params_t *params) {
  if (rc == MANIFEST_DEFAULT_ERR_INVALID_ARG) {
    return "bad_args";
  }
  if (rc == MANIFEST_DEFAULT_ERR_BUFFER_TOO_SMALL) {
    return "output_too_small";
  }
  if (rc != MANIFEST_DEFAULT_ERR_INVALID_FIELD) {
    return "internal_error";
  }
  if (params == 0) {
    return "invalid_field";
  }
  if (params->app_id == 0 || params->app_version == 0 || params->binary_path == 0
      || mg_strlen(params->app_id) == 0u || mg_strlen(params->app_version) == 0u
      || mg_strlen(params->binary_path) == 0u) {
    return "bad_required_fields";
  }
  if (params->subject_id < 1u || params->subject_id > 7u) {
    return "bad_subject_id";
  }
  if (manifest_default_owner_kind_tag(params->owner_kind) == 0) {
    return "bad_owner_kind";
  }
  if (params->runtime_arena_bytes != 0u
      && (params->runtime_arena_bytes < MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES
          || params->runtime_arena_bytes > MANIFEST_DEFAULT_RUNTIME_ARENA_BYTES_MAX)) {
    return "bad_arena_bytes";
  }
  return "invalid_field";
}

manifest_default_result_t manifest_default_format_audit_marker_ok(
    uint32_t              sid,
    const char           *sof_sha_prefix,
    manifest_owner_kind_t owner_kind,
    uint32_t              arena_bytes,
    char                 *out_buf,
    size_t                out_cap,
    size_t               *out_len) {
  size_t pos = 0u;
  size_t i;
  size_t n;
  const char *owner_tag;
  manifest_default_result_t st;

  if (out_buf == 0 || out_len == 0 || out_cap == 0u || sof_sha_prefix == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_ARG;
  }

  owner_tag = manifest_default_owner_kind_tag(owner_kind);
  if (owner_tag == 0) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }

  n = mg_strlen(sof_sha_prefix);
  if (n < MANIFEST_SYNTH_AUDIT_SHA_PREFIX_HEX || n > 64u) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }
  for (i = 0u; i < n; ++i) {
    if (!mg_is_lower_hex(sof_sha_prefix[i])) {
      return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
    }
  }
  if (arena_bytes == 0u) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }

  st = mg_append_str(out_buf, out_cap, &pos, "manifest.synth.ok:");
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_u32(out_buf, out_cap, &pos, sid);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_byte(out_buf, out_cap, &pos, ':');
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_str(out_buf, out_cap, &pos, sof_sha_prefix);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_byte(out_buf, out_cap, &pos, ':');
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_str(out_buf, out_cap, &pos, owner_tag);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_byte(out_buf, out_cap, &pos, ':');
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_u32(out_buf, out_cap, &pos, arena_bytes);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }

  out_buf[pos] = '\0';
  *out_len = pos;
  return MANIFEST_DEFAULT_OK;
}

manifest_default_result_t manifest_default_format_audit_marker_fail(
    uint32_t                        sid,
    manifest_default_result_t       rc,
    const manifest_default_params_t *params,
    char                            *out_buf,
    size_t                           out_cap,
    size_t                          *out_len) {
  size_t pos = 0u;
  const char *reason;
  manifest_default_result_t st;

  if (out_buf == 0 || out_len == 0 || out_cap == 0u) {
    return MANIFEST_DEFAULT_ERR_INVALID_ARG;
  }
  if (rc == MANIFEST_DEFAULT_OK) {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }

  reason = manifest_default_audit_fail_reason(rc, params);
  if (reason == 0 || reason[0] == '\0') {
    return MANIFEST_DEFAULT_ERR_INVALID_FIELD;
  }

  st = mg_append_str(out_buf, out_cap, &pos, "manifest.synth.fail:");
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_u32(out_buf, out_cap, &pos, sid);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_byte(out_buf, out_cap, &pos, ':');
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }
  st = mg_append_str(out_buf, out_cap, &pos, reason);
  if (st != MANIFEST_DEFAULT_OK) {
    return st;
  }

  out_buf[pos] = '\0';
  *out_len = pos;
  return MANIFEST_DEFAULT_OK;
}
