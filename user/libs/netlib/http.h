#ifndef SECUREOS_NET_HTTP_H
#define SECUREOS_NET_HTTP_H

/**
 * @file http.h
 * @brief HTTP/1.1 client request/response interface.
 *
 * Purpose:
 *   Provides a blocking HTTP client that opens a TCP connection, sends an
 *   HTTP/1.1 request (GET or POST with optional custom headers and body),
 *   and returns the response status code and body in caller-supplied buffers.
 *
 * Interactions:
 *   - tcp.c: HTTP uses tcp_connect/send/recv/close.
 *   - dns.c: hostname in URL is resolved before TCP connect.
 *   - process.c: app_sys_http() calls http_request() and prints the result.
 *
 * Launched by:
 *   Called from app_sys_http() during command execution.  Not standalone;
 *   compiled into the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  HTTP_MAX_URL_LEN      = 256,
  HTTP_MAX_HEADER_NAME  = 64,
  HTTP_MAX_HEADER_VAL   = 256,  /* large enough for values with spaces */
  HTTP_MAX_HEADERS      = 8,    /* max request headers caller can inject */
  HTTP_MAX_RESP_HEADERS = 24,   /* max response headers captured */
  HTTP_BODY_MAX         = 4096,
  HTTP_STATUS_LINE_MAX  = 128,
};

typedef enum {
  HTTP_OK = 0,
  HTTP_ERR_BAD_URL     = 1,  /* Could not parse URL                      */
  HTTP_ERR_DNS         = 2,  /* DNS resolution failed                    */
  HTTP_ERR_CONNECT     = 3,  /* TCP connect failed                       */
  HTTP_ERR_SEND        = 4,  /* Failed to transmit request               */
  HTTP_ERR_RECV        = 5,  /* Timeout or connection error on receive   */
  HTTP_ERR_RESPONSE    = 6,  /* Response could not be parsed             */
} http_result_t;

typedef struct {
  char name[HTTP_MAX_HEADER_NAME];
  char value[HTTP_MAX_HEADER_VAL];
} http_header_t;

typedef struct {
  const char *method;                        /* "GET" or "POST"          */
  const char *url;                           /* Full URL string          */
  const http_header_t *extra_headers;        /* Optional custom headers  */
  size_t extra_header_count;
  const char *body;                          /* Optional request body    */
  size_t body_len;                           /* 0 if no body             */
} http_request_t;

typedef struct {
  int status_code;                              /* e.g. 200, 404            */
  char status_line[HTTP_STATUS_LINE_MAX];       /* full first response line */
  http_header_t resp_headers[HTTP_MAX_RESP_HEADERS]; /* parsed response hdrs */
  size_t resp_header_count;
  uint8_t body[HTTP_BODY_MAX];
  size_t body_len;
} http_response_t;

/* Execute a blocking HTTP request.
 * Returns HTTP_OK on success; response is populated in *resp.
 * On error, resp->status_code is 0 and resp->body_len is 0. */
http_result_t http_request(const http_request_t *req, http_response_t *resp);

#endif
