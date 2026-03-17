/**
 * @file http.c
 * @brief HTTP/1.1 client for SecureOS.
 *
 * Purpose:
 *   Implements a blocking HTTP/1.1 client that resolves the target hostname
 *   via DNS, opens a TCP connection, transmits a GET or POST request with
 *   optional custom headers and body, then receives and parses the response
 *   status line and body.  Response body is bounded to HTTP_BODY_MAX bytes.
 *
 * Interactions:
 *   - dns.c: hostname resolved before TCP connect.
 *   - tcp.c: connection lifecycle (connect / send / recv / close).
 *   - process.c: app_sys_http() calls http_request() and prints the result.
 *
 * Launched by:
 *   Called on-demand per user `http` command invocation.  Not a standalone
 *   process; compiled into the kernel image.
 */

#include "http.h"

#include <stddef.h>
#include <stdint.h>

#include "dns.h"
#include "tcp.h"

/* -----------------------------------------------------------------------
 * Minimal string helpers (no libc in freestanding kernel)
 * --------------------------------------------------------------------- */

static size_t http_strlen(const char *s) {
  size_t n = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static void http_memcpy(uint8_t *dst, const void *src, size_t n) {
  const uint8_t *s = (const uint8_t *)src;
  size_t i = 0u;
  for (i = 0u; i < n; ++i) {
    dst[i] = s[i];
  }
}

static void http_memset(void *dst, int val, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  size_t i = 0u;
  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)val;
  }
}

static int http_is_digit(char c) {
  return c >= '0' && c <= '9';
}

static size_t http_append(uint8_t *buf, size_t buf_size, size_t pos, const char *str) {
  size_t slen = http_strlen(str);
  size_t i = 0u;
  for (i = 0u; i < slen && pos + 1u < buf_size; ++i) {
    buf[pos++] = (uint8_t)str[i];
  }
  return pos;
}

static size_t http_append_u32(uint8_t *buf, size_t buf_size, size_t pos, uint32_t val) {
  char digits[11];
  size_t count = 0u;
  size_t i = 0u;

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

/* -----------------------------------------------------------------------
 * URL parsing  (http://host[:port]/path)
 * --------------------------------------------------------------------- */

typedef struct {
  char host[128];
  uint16_t port;
  char path[256];
  int valid;
} http_parsed_url_t;

static void http_parse_url(const char *url, http_parsed_url_t *out) {
  const char *cursor = url;
  size_t host_len = 0u;
  size_t path_len = 0u;
  size_t i = 0u;

  http_memset(out, 0, sizeof(*out));
  out->port = 80u;

  if (url == 0 || url[0] == '\0') {
    return;
  }

  /* Strip "http://" prefix if present */
  if (cursor[0] == 'h' && cursor[1] == 't' && cursor[2] == 't' &&
      cursor[3] == 'p' && cursor[4] == ':' && cursor[5] == '/' &&
      cursor[6] == '/') {
    cursor += 7u;
  } else if (cursor[0] == '/' && cursor[1] == '/') {
    cursor += 2u;
  }

  /* Reject https explicitly (not supported in v1) */
  if (cursor[0] == 's' && !http_is_digit(cursor[0])) {
    /* Check if url started with "https://" */
    const char *u2 = url;
    if (u2[0] == 'h' && u2[1] == 't' && u2[2] == 't' && u2[3] == 'p' &&
        u2[4] == 's') {
      return;  /* HTTPS not supported */
    }
  }

  /* Host: read until '/' or ':' or end */
  host_len = 0u;
  while (cursor[host_len] != '\0' && cursor[host_len] != '/' && cursor[host_len] != ':') {
    if (host_len + 1u < sizeof(out->host)) {
      out->host[host_len] = cursor[host_len];
    }
    ++host_len;
  }
  out->host[host_len < sizeof(out->host) ? host_len : sizeof(out->host) - 1u] = '\0';
  cursor += host_len;

  /* Optional port */
  if (*cursor == ':') {
    ++cursor;
    unsigned int port_val = 0u;
    while (http_is_digit(*cursor)) {
      port_val = port_val * 10u + (unsigned int)(*cursor - '0');
      ++cursor;
    }
    if (port_val > 0u && port_val <= 65535u) {
      out->port = (uint16_t)port_val;
    }
  }

  /* Path */
  if (*cursor == '\0') {
    out->path[0] = '/';
    out->path[1] = '\0';
  } else {
    path_len = http_strlen(cursor);
    for (i = 0u; i < path_len && i + 1u < sizeof(out->path); ++i) {
      out->path[i] = cursor[i];
    }
    out->path[i] = '\0';
  }

  out->valid = (out->host[0] != '\0') ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * Response parsing helpers
 * --------------------------------------------------------------------- */

/* Find first occurrence of needle in haystack (binary safe up to hay_len). */
static const uint8_t *http_find_bytes(const uint8_t *haystack, size_t hay_len,
                                       const char *needle, size_t needle_len) {
  size_t i = 0u;
  if (hay_len < needle_len) {
    return 0;
  }
  for (i = 0u; i <= hay_len - needle_len; ++i) {
    size_t j = 0u;
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

/* Parse status code from "HTTP/1.x NNN …" response line. */
static int http_parse_status_line(const uint8_t *resp, size_t resp_len,
                                   http_response_t *out) {
  const uint8_t *crlf = 0;
  size_t line_len = 0u;
  size_t i = 0u;
  const uint8_t *code_start = 0;

  crlf = http_find_bytes(resp, resp_len, "\r\n", 2u);
  if (crlf == 0) {
    /* Fallback: try LF only */
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

  /* Find status code: skip "HTTP/x.y " */
  code_start = 0;
  for (i = 0u; i + 4u < line_len; ++i) {
    if (resp[i] == ' ' && http_is_digit((char)resp[i + 1u])) {
      code_start = resp + i + 1u;
      break;
    }
  }

  if (code_start != 0) {
    out->status_code = 0;
    for (i = 0u; i < 3u && http_is_digit((char)code_start[i]); ++i) {
      out->status_code = out->status_code * 10 + (code_start[i] - '0');
    }
  }

  return 1;
}

/* Locate response body (after "\r\n\r\n" header separator). */
static const uint8_t *http_find_body(const uint8_t *resp, size_t resp_len,
                                      size_t *out_body_len) {
  const uint8_t *sep = http_find_bytes(resp, resp_len, "\r\n\r\n", 4u);
  if (sep == 0) {
    *out_body_len = 0u;
    return 0;
  }
  sep += 4u;
  *out_body_len = resp_len - (size_t)(sep - resp);
  return sep;
}

/* -----------------------------------------------------------------------
 * Public: http_request
 * --------------------------------------------------------------------- */

http_result_t http_request(const http_request_t *req, http_response_t *resp) {
  http_parsed_url_t parsed;
  uint32_t remote_ip = 0u;
  tcp_conn_t conn;
  static uint8_t req_buf[4096];
  static uint8_t resp_buf[TCP_RESPONSE_MAX];
  size_t req_len = 0u;
  size_t resp_len = 0u;
  tcp_result_t tresult = TCP_OK;
  const char *method = "GET";
  size_t i = 0u;

  if (req == 0 || resp == 0) {
    return HTTP_ERR_BAD_URL;
  }

  http_memset(resp, 0, sizeof(*resp));

  if (req->method != 0 && req->method[0] != '\0') {
    method = req->method;
  }

  http_parse_url(req->url, &parsed);
  if (!parsed.valid) {
    return HTTP_ERR_BAD_URL;
  }

  remote_ip = dns_resolve(parsed.host);
  if (remote_ip == 0u) {
    return HTTP_ERR_DNS;
  }

  tresult = tcp_connect(&conn, remote_ip, parsed.port);
  if (tresult != TCP_OK) {
    return HTTP_ERR_CONNECT;
  }

  /* Build HTTP request */
  req_len = 0u;
  req_len = http_append(req_buf, sizeof(req_buf), req_len, method);
  req_len = http_append(req_buf, sizeof(req_buf), req_len, " ");
  req_len = http_append(req_buf, sizeof(req_buf), req_len, parsed.path);
  req_len = http_append(req_buf, sizeof(req_buf), req_len, " HTTP/1.1\r\n");
  req_len = http_append(req_buf, sizeof(req_buf), req_len, "Host: ");
  req_len = http_append(req_buf, sizeof(req_buf), req_len, parsed.host);
  req_len = http_append(req_buf, sizeof(req_buf), req_len, "\r\n");
  req_len = http_append(req_buf, sizeof(req_buf), req_len, "Connection: close\r\n");
  req_len = http_append(req_buf, sizeof(req_buf), req_len, "User-Agent: SecureOS/1.0\r\n");

  /* Custom headers */
  if (req->extra_headers != 0 && req->extra_header_count > 0u) {
    for (i = 0u; i < req->extra_header_count; ++i) {
      if (req->extra_headers[i].name[0] == '\0') {
        continue;
      }
      req_len = http_append(req_buf, sizeof(req_buf), req_len,
                             req->extra_headers[i].name);
      req_len = http_append(req_buf, sizeof(req_buf), req_len, ": ");
      req_len = http_append(req_buf, sizeof(req_buf), req_len,
                             req->extra_headers[i].value);
      req_len = http_append(req_buf, sizeof(req_buf), req_len, "\r\n");
    }
  }

  /* Content-Length if body present */
  if (req->body != 0 && req->body_len > 0u) {
    req_len = http_append(req_buf, sizeof(req_buf), req_len, "Content-Length: ");
    req_len = http_append_u32(req_buf, sizeof(req_buf), req_len, (uint32_t)req->body_len);
    req_len = http_append(req_buf, sizeof(req_buf), req_len, "\r\n");
  }

  /* Header terminator */
  req_len = http_append(req_buf, sizeof(req_buf), req_len, "\r\n");

  /* Body */
  if (req->body != 0 && req->body_len > 0u) {
    size_t body_copy = req->body_len;
    if (req_len + body_copy > sizeof(req_buf)) {
      body_copy = sizeof(req_buf) - req_len;
    }
    http_memcpy(req_buf + req_len, (const uint8_t *)req->body, body_copy);
    req_len += body_copy;
  }

  tresult = tcp_send(&conn, req_buf, req_len);
  if (tresult != TCP_OK) {
    tcp_close(&conn);
    return HTTP_ERR_SEND;
  }

  resp_len = tcp_recv(&conn, resp_buf, sizeof(resp_buf), TCP_RECV_TIMEOUT);
  tcp_close(&conn);

  if (resp_len == 0u) {
    return HTTP_ERR_RECV;
  }

  if (!http_parse_status_line(resp_buf, resp_len, resp)) {
    return HTTP_ERR_RESPONSE;
  }

  {
    const uint8_t *body = 0;
    size_t body_len = 0u;

    body = http_find_body(resp_buf, resp_len, &body_len);
    if (body != 0 && body_len > 0u) {
      size_t copy = body_len < (size_t)(HTTP_BODY_MAX - 1u) ? body_len : (size_t)(HTTP_BODY_MAX - 1u);
      http_memcpy(resp->body, body, copy);
      resp->body_len = copy;
      resp->body[copy] = 0u;
    }
  }

  return HTTP_OK;
}
