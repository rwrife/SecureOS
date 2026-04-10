#ifndef SECUREOS_NET_HTTPS_H
#define SECUREOS_NET_HTTPS_H

/**
 * @file https.h
 * @brief HTTPS/1.1 client request/response interface.
 *
 * Purpose:
 *   Provides a blocking HTTPS client that layers HTTP/1.1 over TLS 1.2.
 *   Mirrors the http_request API but uses tls_connect/send/recv/close
 *   for encrypted transport.
 *
 * Interactions:
 *   - tls.h provides the encrypted transport layer.
 *   - http.h provides the request/response types (reused by HTTPS).
 *   - dns.h resolves the target hostname.
 *   - http.c delegates https:// URLs to https_request().
 *
 * Launched by:
 *   Called from http_request() when an https:// URL is detected, or
 *   directly from netlib_https_get().  Not standalone.
 */

#include "http.h"

typedef enum {
  HTTPS_OK           = 0,
  HTTPS_ERR_BAD_URL  = 1,  /* Could not parse URL                    */
  HTTPS_ERR_DNS      = 2,  /* DNS resolution failed                  */
  HTTPS_ERR_CONNECT  = 3,  /* TCP connect failed                     */
  HTTPS_ERR_TLS      = 4,  /* TLS handshake or cert error            */
  HTTPS_ERR_SEND     = 5,  /* Failed to transmit request             */
  HTTPS_ERR_RECV     = 6,  /* Timeout or connection error on receive */
  HTTPS_ERR_RESPONSE = 7,  /* Response could not be parsed           */
} https_result_t;

/**
 * Execute a blocking HTTPS request.
 *
 * Uses the same http_request_t and http_response_t structures as the
 * HTTP client.  The URL must begin with "https://".
 *
 * Returns HTTPS_OK on success; response is populated in *resp.
 */
https_result_t https_request(const http_request_t *req, http_response_t *resp);

#endif