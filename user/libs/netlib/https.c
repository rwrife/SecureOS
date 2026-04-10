/**
 * @file https.c
 * @brief HTTPS/1.1 client implementation over TLS 1.2.
 *
 * Purpose:
 *   Implements a blocking HTTPS client that resolves the target hostname
 *   via DNS, opens a TLS connection (which includes TCP + TLS handshake),
 *   transmits an HTTP/1.1 request over the encrypted channel, and receives
 *   and parses the response.
 *
 * Interactions:
 *   - https.h declares the public API.
 *   - tls.h provides encrypted connect/send/recv/close.
 *   - dns.h resolves hostnames.
 *   - http.h provides request/response types and parsing helpers.
 *
 * Launched by:
 *   Called from http_request() when an https:// URL is detected, or
 *   directly from netlib_https_get() in api.c.  Not standalone.
 */

#include "https.h"

#include <stddef.h>
#include <stdint.h>

#include "dns.h"
#include "tls.h"

/* ---- Local helpers (mirror http.c patterns) --------------------------- */

static size_t https_strlen(const char *s) {
  size_t n = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static void https_memcpy(uint8_t *dst, const void *src, size_t n) {
  const uint8_t *s = (const uint8_t *)src;
  size_t i;
  for (i = 0u; i < n; ++i) {
    dst[i] = s[i];
  }
}

static void https_memset(void *dst, int val, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  size_t i;
  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)val;
  }
}

static int https_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static size_t https_append(uint8_t *buf, size_t buf_size, size_t pos,
                           const char *str) {
  size_t slen = https_strlen(str);
  size_t i;
  for (i = 0u; i < slen && pos + 1u < buf_size; ++i) {
    buf[pos++] = (uint8_t)str[i];
  }
  return pos;
}

static size_t https_append_u32(uint8_t *buf, size_t buf_size, size_t pos,
                               uint32_t val) {
  char digits[11];
  size_t count = 0u;
  size_t i;

  if (val == 0u) {
    if (pos + 1u < buf_size) {
      buf[pos++] = '0';
    }
    return pos;
  }
  while (val > 0u && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (val % 10u));
    val /= 10u;
  }
  for (i = 0u; i < count; ++i) {
    if (pos + 1u >= buf_size) {
      break;
    }
    buf[pos++] = (uint8_t)digits[count - i - 1u];
  }
  return pos;
}

/* ---- URL parsing for https:// ----------------------------------------- */

typedef struct {
  char host[128];
  uint16_t port;
  char path[256];
  int valid;
} https_parsed_url_t;

static void https_parse_url(const char *url, https_parsed_url_t *out) {
  const char *cursor = url;
  size_t host_len = 0u;
  size_t path_len = 0u;
  size_t i;

  https_memset(out, 0, sizeof(*out));
  out->port = 443u;

  if (url == 0 || url[0] == '\0') {
    return;
  }

  /* Strip https:// prefix */
  if (cursor[0] == 'h' && cursor[1] == 't' && cursor[2] == 't' &&
      cursor[3] == 'p' && cursor[4] == 's' && cursor[5] == ':' &&
      cursor[6] == '/' && cursor[7] == '/') {
    cursor += 8u;
  } else if (cursor[0] == '/' && cursor[1] == '/') {
    cursor += 2u;
  } else {
    /* Not an https URL */
    return;
  }

  /* Parse host */
  host_len = 0u;
  while (cursor[host_len] != '\0' && cursor[host_len] != '/' &&
         cursor[host_len] != ':') {
    if (host_len + 1u < sizeof(out->host)) {
      out->host[host_len] = cursor[host_len];
    }
    ++host_len;
  }
  out->host[host_len < sizeof(out->host) ? host_len
                                          : sizeof(out->host) - 1u] = '\0';
  cursor += host_len;

  /* Parse optional port */
  if (*cursor == ':') {
    unsigned int port_val = 0u;
    ++cursor;
    while (https_is_digit(*cursor)) {
      port_val = port_val * 10u + (unsigned int)(*cursor - '0');
      ++cursor;
    }
    if (port_val > 0u && port_val <= 65535u) {
      out->port = (uint16_t)port_val;
    }
  }

  /* Parse path */
  if (*cursor == '\0') {
    out->path[0] = '/';
    out->path[1] = '\0';
  } else {
    path_len = https_strlen(cursor);
    for (i = 0u; i < path_len && i + 1u < sizeof(out->path); ++i) {
      out->path[i] = cursor[i];
    }
    out->path[i] = '\0';
  }

  out->valid = (out->host[0] != '\0') ? 1 : 0;
}

/* ---- Response parsing helpers (same as http.c) ------------------------ */

static const uint8_t *https_find_bytes(const uint8_t *haystack, size_t hay_len,
                                       const char *needle, size_t needle_len) {
  size_t i;
  if (hay_len < needle_len) {
    return 0;
  }
  for (i = 0u; i <= hay_len - needle_len; ++i) {
    size_t j;
    int match = 1;
    for (j = 0u; j < needle_len; ++j) {
      if (haystack[i + j] != (uint8_t)needle[j]) {
        match = 0;
        break;
      }
    }
    if (match) {
      return haystack + i;
    }
  }
  return 0;
}

static int https_parse_status_line(const uint8_t *resp, size_t resp_len,
                                   http_response_t *out) {
  const uint8_t *crlf;
  size_t line_len;
  size_t i;
  const uint8_t *code_start;

  crlf = https_find_bytes(resp, resp_len, "\r\n", 2u);
  if (crlf == 0) {
    for (i = 0u; i < resp_len; ++i) {
      if (resp[i] == '\n') {
        crlf = resp + i;
        break;
      }
    }
    if (crlf == 0) {
      return 0;
    }
  }

  line_len = (size_t)(crlf - resp);
  if (line_len >= (size_t)HTTP_STATUS_LINE_MAX) {
    line_len = (size_t)HTTP_STATUS_LINE_MAX - 1u;
  }
  for (i = 0u; i < line_len; ++i) {
    out->status_line[i] = (char)resp[i];
  }
  out->status_line[line_len] = '\0';

  code_start = 0;
  for (i = 0u; i + 4u < line_len; ++i) {
    if (resp[i] == ' ' && https_is_digit((char)resp[i + 1u])) {
      code_start = resp + i + 1u;
      break;
    }
  }

  if (code_start != 0) {
    out->status_code = 0;
    for (i = 0u; i < 3u && https_is_digit((char)code_start[i]); ++i) {
      out->status_code = out->status_code * 10 + (code_start[i] - '0');
    }
  }

  return 1;
}

static const uint8_t *https_find_body(const uint8_t *resp, size_t resp_len,
                                      size_t *out_body_len) {
  const uint8_t *sep = https_find_bytes(resp, resp_len, "\r\n\r\n", 4u);
  if (sep == 0) {
    *out_body_len = 0u;
    return 0;
  }
  sep += 4u;
  *out_body_len = resp_len - (size_t)(sep - resp);
  return sep;
}

/* ---- Public API ------------------------------------------------------- */

https_result_t https_request(const http_request_t *req,
                             http_response_t *resp) {
  https_parsed_url_t parsed;
  uint32_t remote_ip;
  static tls_conn_t conn;
  static uint8_t req_buf[4096];
  static uint8_t resp_buf[4096];
  size_t req_len = 0u;
  size_t resp_len;
  tls_result_t tres;
  const char *method = "GET";
  size_t i;

  if (req == 0 || resp == 0) {
    return HTTPS_ERR_BAD_URL;
  }

  https_memset(resp, 0, sizeof(*resp));

  if (req->method != 0 && req->method[0] != '\0') {
    method = req->method;
  }

  https_parse_url(req->url, &parsed);
  if (!parsed.valid) {
    return HTTPS_ERR_BAD_URL;
  }

  /* DNS resolve */
  remote_ip = dns_resolve(parsed.host);
  if (remote_ip == 0u) {
    return HTTPS_ERR_DNS;
  }

  /* TLS connect (includes TCP + TLS handshake) */
  tres = tls_connect(&conn, remote_ip, parsed.port, parsed.host);
  if (tres == TLS_ERR_CONNECT) {
    return HTTPS_ERR_CONNECT;
  }
  if (tres == TLS_ERR_CERT) {
    return HTTPS_ERR_TLS;
  }
  if (tres != TLS_OK) {
    return HTTPS_ERR_TLS;
  }

  /* Build HTTP/1.1 request */
  req_len = 0u;
  req_len = https_append(req_buf, sizeof(req_buf), req_len, method);
  req_len = https_append(req_buf, sizeof(req_buf), req_len, " ");
  req_len = https_append(req_buf, sizeof(req_buf), req_len, parsed.path);
  req_len = https_append(req_buf, sizeof(req_buf), req_len, " HTTP/1.1\r\n");
  req_len = https_append(req_buf, sizeof(req_buf), req_len, "Host: ");
  req_len = https_append(req_buf, sizeof(req_buf), req_len, parsed.host);
  req_len = https_append(req_buf, sizeof(req_buf), req_len, "\r\n");
  req_len = https_append(req_buf, sizeof(req_buf), req_len,
                         "Connection: close\r\n");
  req_len = https_append(req_buf, sizeof(req_buf), req_len,
                         "User-Agent: SecureOS/1.0\r\n");

  /* Custom headers */
  if (req->extra_headers != 0 && req->extra_header_count > 0u) {
    for (i = 0u; i < req->extra_header_count; ++i) {
      if (req->extra_headers[i].name[0] == '\0') {
        continue;
      }
      req_len = https_append(req_buf, sizeof(req_buf), req_len,
                             req->extra_headers[i].name);
      req_len = https_append(req_buf, sizeof(req_buf), req_len, ": ");
      req_len = https_append(req_buf, sizeof(req_buf), req_len,
                             req->extra_headers[i].value);
      req_len = https_append(req_buf, sizeof(req_buf), req_len, "\r\n");
    }
  }

  /* Body */
  if (req->body != 0 && req->body_len > 0u) {
    req_len = https_append(req_buf, sizeof(req_buf), req_len,
                           "Content-Length: ");
    req_len = https_append_u32(req_buf, sizeof(req_buf), req_len,
                               (uint32_t)req->body_len);
    req_len = https_append(req_buf, sizeof(req_buf), req_len, "\r\n");
  }

  req_len = https_append(req_buf, sizeof(req_buf), req_len, "\r\n");

  /* Append body data */
  if (req->body != 0 && req->body_len > 0u) {
    size_t body_copy = req->body_len;
    if (req_len + body_copy > sizeof(req_buf)) {
      body_copy = sizeof(req_buf) - req_len;
    }
    https_memcpy(req_buf + req_len, (const uint8_t *)req->body, body_copy);
    req_len += body_copy;
  }

  /* Send over TLS */
  tres = tls_send(&conn, req_buf, req_len);
  if (tres != TLS_OK) {
    tls_close(&conn);
    return HTTPS_ERR_SEND;
  }

  /* Receive response over TLS */
  resp_len = tls_recv(&conn, resp_buf, sizeof(resp_buf),
                      (uint32_t)TLS_RECV_TIMEOUT);
  tls_close(&conn);

  if (resp_len == 0u) {
    return HTTPS_ERR_RECV;
  }

  /* Parse response status line */
  if (!https_parse_status_line(resp_buf, resp_len, resp)) {
    return HTTPS_ERR_RESPONSE;
  }

  /* Extract body */
  {
    const uint8_t *body;
    size_t body_len;

    body = https_find_body(resp_buf, resp_len, &body_len);
    if (body != 0 && body_len > 0u) {
      size_t copy = body_len < (size_t)(HTTP_BODY_MAX - 1u)
                        ? body_len
                        : (size_t)(HTTP_BODY_MAX - 1u);
      https_memcpy(resp->body, body, copy);
      resp->body_len = copy;
      resp->body[copy] = 0u;
    }
  }

  return HTTPS_OK;
}