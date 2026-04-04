#ifndef SECUREOS_NET_TLS_H
#define SECUREOS_NET_TLS_H

/**
 * @file tls.h
 * @brief TLS 1.2 client session types and connection interface.
 *
 * Purpose:
 *   Provides a blocking TLS 1.2 client that wraps the existing TCP
 *   connection layer with BearSSL's TLS engine.  Handles handshake,
 *   encrypted send/recv, and teardown.
 *
 * Interactions:
 *   - tcp.h provides the underlying TCP transport.
 *   - BearSSL (vendor/bearssl) provides the TLS engine and X.509 validator.
 *   - ca_bundle.h provides trusted CA certificates.
 *   - entropy.h provides PRNG seeding.
 *   - https.c calls tls_connect/send/recv/close for HTTPS requests.
 *
 * Launched by:
 *   Called on-demand from https.c.  Not standalone.
 */

#include <stddef.h>
#include <stdint.h>

#include "tcp.h"

typedef enum {
  TLS_OK            = 0,
  TLS_ERR_HANDSHAKE = 1,  /* TLS handshake failed                    */
  TLS_ERR_CERT      = 2,  /* Server certificate verification failed  */
  TLS_ERR_SEND      = 3,  /* Encrypted send failed                   */
  TLS_ERR_RECV      = 4,  /* Encrypted recv failed or timeout        */
  TLS_ERR_CLOSED    = 5,  /* Server closed connection                */
  TLS_ERR_NO_MEMORY = 6,  /* Static buffer exhausted                 */
  TLS_ERR_CONNECT   = 7,  /* TCP connect failed                      */
} tls_result_t;

enum {
  TLS_IO_BUF_SIZE   = 16384,  /* BearSSL I/O buffer (send + receive)  */
  TLS_DEFAULT_PORT  = 443,
  TLS_HANDSHAKE_TIMEOUT = 2000000, /* poll iterations for handshake   */
  TLS_RECV_TIMEOUT  = 1000000,     /* poll iterations for data recv   */
};

/**
 * TLS connection state.
 *
 * This struct is large (~16KB+ due to the I/O buffer and BearSSL
 * context) and should be declared as a static or global variable,
 * not on the stack in a freestanding environment.
 *
 * The internal fields reference BearSSL types when BearSSL headers
 * are included.  The implementation in tls.c handles all BearSSL
 * interaction — callers only use the tls_* API.
 */
typedef struct {
  tcp_conn_t tcp;                    /* underlying TCP connection       */
  uint8_t iobuf[TLS_IO_BUF_SIZE];   /* BearSSL bidirectional I/O buf   */
  int handshake_done;                /* 1 if TLS handshake completed    */
  int closed;                        /* 1 if connection is closed       */
  /* BearSSL engine state is stored in tls.c static storage because
   * br_ssl_client_context and br_x509_minimal_context are large and
   * their size depends on BearSSL headers.  Only one TLS connection
   * is active at a time (matching the single-TCP-connection model). */
} tls_conn_t;

/**
 * Establish a TLS connection to a remote server.
 *
 * Opens a TCP connection to remote_ip:remote_port, initializes the
 * BearSSL TLS client engine with the embedded CA trust anchors and
 * the given server hostname (for SNI), and performs the TLS handshake.
 *
 * hostname is used for Server Name Indication (SNI) and certificate
 * common name / SAN matching.  Must be a null-terminated string.
 *
 * Returns TLS_OK on success.
 */
tls_result_t tls_connect(tls_conn_t *conn,
                         uint32_t remote_ip,
                         uint16_t remote_port,
                         const char *hostname);

/**
 * Send plaintext data over the TLS connection.
 *
 * Encrypts data using the BearSSL engine and transmits the resulting
 * TLS records over the underlying TCP connection.
 *
 * Returns TLS_OK on success.
 */
tls_result_t tls_send(tls_conn_t *conn,
                      const uint8_t *data,
                      size_t len);

/**
 * Receive plaintext data from the TLS connection.
 *
 * Polls the TCP connection for encrypted data, feeds it through the
 * BearSSL engine, and returns decrypted plaintext.
 *
 * Returns the number of bytes received (0 on timeout or error).
 */
size_t tls_recv(tls_conn_t *conn,
                uint8_t *buf_out,
                size_t buf_size,
                uint32_t timeout);

/**
 * Close the TLS connection.
 *
 * Sends a TLS close_notify alert and closes the underlying TCP
 * connection.
 */
void tls_close(tls_conn_t *conn);

#endif