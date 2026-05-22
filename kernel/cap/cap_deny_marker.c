/**
 * @file cap_deny_marker.c
 * @brief Implementation of the canonical CAP:DENY:<...> serial marker.
 *
 * See `cap_deny_marker.h` and `docs/abi/capability-deny-contract.md` §4 for
 * the wire grammar this file is the single source of truth for.
 *
 * Issue: #211 (conformance test for capability-denied log marker shape).
 *
 * Pure C11, freestanding-friendly. No <stdio.h>, no allocations, no globals.
 */

#include "cap_deny_marker.h"

#include <stddef.h>

/* Local helpers — keep visible only to this TU. */

static int cdm_is_printable_ascii(unsigned char c) {
  return c >= 0x20u && c < 0x7Fu;
}

/* Append a NUL-terminated string into buf at *pos within buf_size.
 * Returns 0 on success, -1 if the destination would overflow. */
static int cdm_append(const char *src, char *buf, size_t buf_size, size_t *pos) {
  size_t i = 0u;
  while (src[i] != '\0') {
    if (*pos >= buf_size) {
      return -1;
    }
    buf[*pos] = src[i];
    (*pos)++;
    i++;
  }
  return 0;
}

/* Append decimal representation of u into buf. */
static int cdm_append_u32(uint32_t u, char *buf, size_t buf_size, size_t *pos) {
  /* uint32_t fits in 10 decimal digits. */
  char tmp[16];
  size_t n = 0u;
  if (u == 0u) {
    tmp[n++] = '0';
  } else {
    while (u != 0u) {
      tmp[n++] = (char)('0' + (u % 10u));
      u /= 10u;
    }
  }
  /* Reverse into buf. */
  while (n > 0u) {
    if (*pos >= buf_size) {
      return -1;
    }
    buf[*pos] = tmp[n - 1u];
    (*pos)++;
    n--;
  }
  return 0;
}

/* Capability id → canonical lowercase name (CAP_ prefix stripped).
 * Order matches capability.h enum order; the table is the single source of
 * truth for the marker's <capability_id> field. */
typedef struct {
  capability_id_t id;
  const char *name;
} cdm_cap_name_entry_t;

static const cdm_cap_name_entry_t cdm_cap_names[] = {
    {CAP_CONSOLE_WRITE, "console_write"},
    {CAP_SERIAL_WRITE, "serial_write"},
    {CAP_DEBUG_EXIT, "debug_exit"},
    {CAP_CAPABILITY_ADMIN, "capability_admin"},
    {CAP_DISK_IO_REQUEST, "disk_io_request"},
    {CAP_FS_READ, "fs_read"},
    {CAP_FS_WRITE, "fs_write"},
    {CAP_EVENT_SUBSCRIBE, "event_subscribe"},
    {CAP_EVENT_PUBLISH, "event_publish"},
    {CAP_APP_EXEC, "app_exec"},
    {CAP_CODESIGN_BYPASS, "codesign_bypass"},
    {CAP_NETWORK, "network"},
    {CAP_SYSCALL, "syscall"},
};

const char *cap_deny_marker_name(capability_id_t cap_id) {
  const size_t n = sizeof(cdm_cap_names) / sizeof(cdm_cap_names[0]);
  for (size_t i = 0u; i < n; ++i) {
    if (cdm_cap_names[i].id == cap_id) {
      return cdm_cap_names[i].name;
    }
  }
  return (const char *)0;
}

int cap_deny_marker_format(cap_subject_id_t actor_subject_id,
                           capability_id_t cap_id,
                           const char *resource,
                           char *buf,
                           size_t buf_size) {
  if (buf == (char *)0 || resource == (const char *)0 || buf_size == 0u) {
    return -1;
  }
  const char *cap_name = cap_deny_marker_name(cap_id);
  if (cap_name == (const char *)0) {
    return -1;
  }

  /* Validate resource. */
  size_t rlen = 0u;
  while (resource[rlen] != '\0') {
    if (rlen >= CAP_DENY_RESOURCE_MAX) {
      return -1;
    }
    unsigned char c = (unsigned char)resource[rlen];
    if (c == ':' || c == '\n' || !cdm_is_printable_ascii(c)) {
      return -1;
    }
    rlen++;
  }
  if (rlen == 0u) {
    return -1;
  }

  size_t pos = 0u;
  if (cdm_append("CAP:DENY:", buf, buf_size, &pos) != 0) return -1;
  if (cdm_append_u32((uint32_t)actor_subject_id, buf, buf_size, &pos) != 0) return -1;
  if (cdm_append(":", buf, buf_size, &pos) != 0) return -1;
  if (cdm_append(cap_name, buf, buf_size, &pos) != 0) return -1;
  if (cdm_append(":", buf, buf_size, &pos) != 0) return -1;
  if (cdm_append(resource, buf, buf_size, &pos) != 0) return -1;
  if (pos >= buf_size) return -1;
  buf[pos++] = '\n';
  if (pos >= buf_size) return -1;
  buf[pos] = '\0';
  return (int)pos;
}

/* Tiny copy helper for reason strings. Truncates safely. */
static void cdm_set_reason(char *reason, size_t reason_size, const char *src) {
  if (reason == (char *)0 || reason_size == 0u) return;
  size_t i = 0u;
  while (src[i] != '\0' && i + 1u < reason_size) {
    reason[i] = src[i];
    i++;
  }
  reason[i] = '\0';
}

int cap_deny_marker_validate(const char *line,
                             char *reason,
                             size_t reason_size) {
  if (line == (const char *)0) {
    cdm_set_reason(reason, reason_size, "null_line");
    return -1;
  }
  /* Compute length (exclude any trailing NUL). */
  size_t len = 0u;
  while (line[len] != '\0') {
    if (len >= CAP_DENY_MARKER_MAX) {
      cdm_set_reason(reason, reason_size, "line_too_long");
      return -1;
    }
    len++;
  }

  /* Must start with the prefix. */
  static const char prefix[] = "CAP:DENY:";
  const size_t plen = sizeof(prefix) - 1u;
  if (len < plen) {
    cdm_set_reason(reason, reason_size, "missing_prefix");
    return -1;
  }
  for (size_t i = 0u; i < plen; ++i) {
    if (line[i] != prefix[i]) {
      cdm_set_reason(reason, reason_size, "missing_prefix");
      return -1;
    }
  }

  /* Must end with exactly one '\n', and that '\n' must be the last byte. */
  if (line[len - 1u] != '\n') {
    cdm_set_reason(reason, reason_size, "missing_newline");
    return -1;
  }
  /* No earlier '\n' allowed. */
  for (size_t i = plen; i + 1u < len; ++i) {
    if (line[i] == '\n') {
      cdm_set_reason(reason, reason_size, "trailing_garbage");
      return -1;
    }
  }

  /* Body = bytes after prefix, before the terminating '\n'. */
  const size_t body_start = plen;
  const size_t body_end = len - 1u; /* index of '\n' */
  if (body_end <= body_start) {
    cdm_set_reason(reason, reason_size, "field_count");
    return -1;
  }

  /* Split body into exactly 3 fields by ':' (actor, cap, resource). */
  size_t field_starts[3];
  size_t field_ends[3];
  size_t fld = 0u;
  field_starts[0] = body_start;
  for (size_t i = body_start; i < body_end; ++i) {
    if (line[i] == ':') {
      if (fld >= 2u) {
        cdm_set_reason(reason, reason_size, "field_count");
        return -1;
      }
      field_ends[fld] = i;
      fld++;
      field_starts[fld] = i + 1u;
    }
  }
  if (fld != 2u) {
    cdm_set_reason(reason, reason_size, "field_count");
    return -1;
  }
  field_ends[2] = body_end;

  /* Field 0: actor — non-empty, all decimal digits. */
  if (field_ends[0] == field_starts[0]) {
    cdm_set_reason(reason, reason_size, "actor_not_decimal");
    return -1;
  }
  for (size_t i = field_starts[0]; i < field_ends[0]; ++i) {
    char c = line[i];
    if (c < '0' || c > '9') {
      cdm_set_reason(reason, reason_size, "actor_not_decimal");
      return -1;
    }
  }

  /* Field 1: cap_name — must match a registered cap name from the table. */
  {
    const size_t cap_len = field_ends[1] - field_starts[1];
    int matched = 0;
    const size_t n = sizeof(cdm_cap_names) / sizeof(cdm_cap_names[0]);
    for (size_t i = 0u; i < n && !matched; ++i) {
      const char *nm = cdm_cap_names[i].name;
      size_t k = 0u;
      while (nm[k] != '\0') k++;
      if (k == cap_len) {
        int eq = 1;
        for (size_t j = 0u; j < cap_len; ++j) {
          if (line[field_starts[1] + j] != nm[j]) { eq = 0; break; }
        }
        if (eq) matched = 1;
      }
    }
    if (!matched) {
      cdm_set_reason(reason, reason_size, "cap_unknown");
      return -1;
    }
  }

  /* Field 2: resource — non-empty, every byte printable ASCII, no ':'. */
  if (field_ends[2] == field_starts[2]) {
    cdm_set_reason(reason, reason_size, "resource_empty");
    return -1;
  }
  for (size_t i = field_starts[2]; i < field_ends[2]; ++i) {
    unsigned char c = (unsigned char)line[i];
    if (c == ':' || c == '\n' || !cdm_is_printable_ascii(c)) {
      cdm_set_reason(reason, reason_size, "resource_bad_byte");
      return -1;
    }
  }

  if (reason != (char *)0 && reason_size > 0u) {
    reason[0] = '\0';
  }
  return 0;
}
