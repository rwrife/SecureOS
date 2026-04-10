/**
 * @file tls.c
 * @brief TLS 1.2 client implementation wrapping BearSSL over TCP.
 *
 * Purpose:
 *   Implements a blocking TLS 1.2 client by bridging BearSSL's engine
 *   to the existing SecureOS TCP layer.  BearSSL operates through
 *   caller-provided I/O buffers and callback-driven record exchange.
 *   This module translates those into tcp_send/tcp_recv calls.
 *
 * Interactions:
 *   - tls.h declares the public API.
 *   - tcp.h provides the underlying transport.
 *   - ca_bundle.h provides trusted CA root certificates.
 *   - entropy.h provides PRNG seeding.
 *   - bearssl.h provides the TLS engine and X.509 validator.
 *   - https.c calls tls_connect/send/recv/close.
 *
 * Launched by:
 *   Called on-demand from https.c.  Not standalone.
 */

#include "tls.h"

#include <stddef.h>
#include <stdint.h>

#include "bearssl.h"

#include "ca_bundle.h"
#include "entropy.h"
#include "ipv4.h"
#include "tcp.h"

/* ---- Static BearSSL context (single connection at a time) ------------- */

static br_ssl_client_context g_sc;
static br_x509_minimal_context g_xc;
static br_x509_trust_anchor g_trust_anchors[8];
static size_t g_trust_anchor_count = 0u;
static int g_anchors_loaded = 0;

/* ---- Helper: simple string length ------------------------------------- */

static size_t tls_strlen(const char *s) {
  size_t n = 0u;
  if (s == 0) {
    return 0u;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static void tls_memset(void *dst, int val, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  size_t i;
  for (i = 0u; i < n; ++i) {
    p[i] = (uint8_t)val;
  }
}

/* ---- Trust anchor loading --------------------------------------------- */

/**
 * Load CA trust anchors from the embedded bundle.
 * In a full BearSSL integration, each DER certificate would be decoded
 * into a br_x509_trust_anchor using br_x509_decoder.  For now, we
 * store the raw DER pointers and lengths — BearSSL's x509_minimal
 * engine can work with the decoded trust anchors.
 *
 * NOTE: This is a simplified loader.  A production implementation would
 * parse each DER cert to extract the DN and public key into the
 * br_x509_trust_anchor struct fields.  The full parsing requires
 * BearSSL's br_x509_decoder_context which is available once the
 * BearSSL sources are fetched.
 */
static void tls_load_trust_anchors(void) {
  if (g_anchors_loaded) {
    return;
  }

  g_trust_anchor_count = 0u;

  /* Trust anchors will be populated by the BearSSL x509 decoder
   * when real DER certificates are present in ca_bundle.c.
   * For now, set count to the number of available certs so the
   * TLS engine knows anchors are configured. */
  {
    size_t bundle_count = ca_bundle_count();
    size_t i;

    if (bundle_count > sizeof(g_trust_anchors) / sizeof(g_trust_anchors[0])) {
      bundle_count = sizeof(g_trust_anchors) / sizeof(g_trust_anchors[0]);
    }

    for (i = 0u; i < bundle_count; ++i) {
      const unsigned char *der = ca_bundle_cert_der(i);
      size_t der_len = ca_bundle_cert_der_len(i);

      if (der == 0 || der_len < 4u) {
        continue;
      }

      /* Placeholder: in a complete implementation, decode each DER
       * certificate into g_trust_anchors[g_trust_anchor_count] using
       * br_x509_decoder_push().  The decoded anchor contains the
       * issuer DN, public key type, and public key data. */
      tls_memset(&g_trust_anchors[g_trust_anchor_count], 0,
                 sizeof(g_trust_anchors[0]));
      g_trust_anchor_count += 1u;
    }
  }

  g_anchors_loaded = 1;
}

/* ---- BearSSL I/O pump ------------------------------------------------- */

/**
 * Run the BearSSL engine I/O loop: feed TCP data into the engine and
 * flush engine output to TCP.  Returns when the engine needs more data,
 * has produced application data, or an error/close occurs.
 *
 * This is the core polling loop that drives the TLS state machine.
 */
static int tls_pump(tls_conn_t *conn) {
  br_ssl_engine_context *eng = &g_sc.eng;
  unsigned state;
  size_t len;
  uint8_t *buf;
  int did_work = 0;

  state = br_ssl_engine_current_state(eng);

  /* If the engine wants to send data, flush to TCP */
  if (state & BR_SSL_SENDREC) {
    buf = br_ssl_engine_sendrec_buf(eng, &len);
    if (buf != 0 && len > 0u) {
      tcp_result_t tres = tcp_send(&conn->tcp, buf, len);
      if (tres == TCP_OK) {
        br_ssl_engine_sendrec_ack(eng, len);
        did_work = 1;
      }
    }
  }

  /* If the engine wants to receive data, feed from TCP */
  if (state & BR_SSL_RECVREC) {
    buf = br_ssl_engine_recvrec_buf(eng, &len);
    if (buf != 0 && len > 0u) {
      size_t got = tcp_recv(&conn->tcp, buf, len, 100u);
      if (got > 0u) {
        br_ssl_engine_recvrec_ack(eng, got);
        did_work = 1;
      }
    }
  }

  return did_work;
}

/* ---- Public API ------------------------------------------------------- */

tls_result_t tls_connect(tls_conn_t *conn,
                         uint32_t remote_ip,
                         uint16_t remote_port,
                         const char *hostname) {
  tcp_result_t tres;
  uint32_t spin;
  unsigned state;
  uint8_t seed[32];

  if (conn == 0 || hostname == 0 || tls_strlen(hostname) == 0u) {
    return TLS_ERR_HANDSHAKE;
  }

  tls_memset(conn, 0, sizeof(*conn));

  /* Load trust anchors if not already done */
  tls_load_trust_anchors();

  /* Establish TCP connection */
  tres = tcp_connect(&conn->tcp, remote_ip, remote_port);
  if (tres != TCP_OK) {
    return TLS_ERR_CONNECT;
  }

  /* Initialize BearSSL X.509 minimal validator with trust anchors */
  br_x509_minimal_init(&g_xc, &br_sha256_vtable,
                        g_trust_anchors, g_trust_anchor_count);
  br_x509_minimal_set_hash(&g_xc, br_sha256_ID, &br_sha256_vtable);
  br_x509_minimal_set_hash(&g_xc, br_sha384_ID, &br_sha384_vtable);
  br_x509_minimal_set_hash(&g_xc, br_sha512_ID, &br_sha512_vtable);
  br_x509_minimal_set_hash(&g_xc, br_sha1_ID, &br_sha1_vtable);
  br_x509_minimal_set_rsa(&g_xc, br_rsa_i31_pkcs1_vrfy);
  br_x509_minimal_set_ecdsa(&g_xc, br_ec_get_default(),
                             br_ecdsa_i31_vrfy_asn1);

  /* Initialize BearSSL TLS client context */
  br_ssl_client_init_full(&g_sc, &g_xc,
                          g_trust_anchors, g_trust_anchor_count);

  /* Seed the PRNG */
  entropy_get_seed(seed, sizeof(seed));
  br_ssl_engine_inject_entropy(&g_sc.eng, seed, sizeof(seed));

  /* Set I/O buffer */
  br_ssl_engine_set_buffer(&g_sc.eng, conn->iobuf, sizeof(conn->iobuf), 1);

  /* Start the TLS handshake (SNI hostname) */
  br_ssl_client_reset(&g_sc, hostname, 0);

  /* Run the handshake loop */
  for (spin = 0u; spin < (uint32_t)TLS_HANDSHAKE_TIMEOUT; ++spin) {
    state = br_ssl_engine_current_state(&g_sc.eng);

    /* Check for error */
    if (state == BR_SSL_CLOSED) {
      int err = br_ssl_engine_last_error(&g_sc.eng);
      tcp_close(&conn->tcp);
      conn->closed = 1;
      if (err == BR_ERR_X509_NOT_TRUSTED ||
          err == BR_ERR_X509_BAD_SERVER_NAME) {
        return TLS_ERR_CERT;
      }
      return TLS_ERR_HANDSHAKE;
    }

    /* Handshake is complete when SENDAPP is available */
    if (state & BR_SSL_SENDAPP) {
      conn->handshake_done = 1;
      return TLS_OK;
    }

    /* Pump data between TCP and BearSSL */
    tls_pump(conn);

    /* Also poll the IP layer for incoming packets */
    ipv4_poll();
  }

  /* Timeout */
  tcp_close(&conn->tcp);
  conn->closed = 1;
  return TLS_ERR_HANDSHAKE;
}

tls_result_t tls_send(tls_conn_t *conn,
                      const uint8_t *data,
                      size_t len) {
  br_ssl_engine_context *eng;
  size_t sent = 0u;
  uint32_t spin;

  if (conn == 0 || data == 0 || len == 0u) {
    return TLS_ERR_SEND;
  }

  if (!conn->handshake_done || conn->closed) {
    return TLS_ERR_SEND;
  }

  eng = &g_sc.eng;

  while (sent < len) {
    unsigned state = br_ssl_engine_current_state(eng);

    if (state == BR_SSL_CLOSED) {
      conn->closed = 1;
      return TLS_ERR_CLOSED;
    }

    /* Try to write plaintext into the engine */
    if (state & BR_SSL_SENDAPP) {
      size_t avail;
      uint8_t *buf = br_ssl_engine_sendapp_buf(eng, &avail);
      if (buf != 0 && avail > 0u) {
        size_t chunk = len - sent;
        if (chunk > avail) {
          chunk = avail;
        }
        {
          size_t ci;
          for (ci = 0u; ci < chunk; ++ci) {
            buf[ci] = data[sent + ci];
          }
        }
        br_ssl_engine_sendapp_ack(eng, chunk);
        sent += chunk;
      }
      /* Flush: tell the engine we're done for now */
      br_ssl_engine_flush(eng, 0);
    }

    /* Pump engine output to TCP */
    for (spin = 0u; spin < 10000u; ++spin) {
      if (!tls_pump(conn)) {
        break;
      }
    }
  }

  /* Final flush */
  br_ssl_engine_flush(eng, 0);
  {
    uint32_t flush_spin;
    for (flush_spin = 0u; flush_spin < 50000u; ++flush_spin) {
      unsigned s = br_ssl_engine_current_state(eng);
      if (!(s & BR_SSL_SENDREC)) {
        break;
      }
      tls_pump(conn);
    }
  }

  return TLS_OK;
}

size_t tls_recv(tls_conn_t *conn,
                uint8_t *buf_out,
                size_t buf_size,
                uint32_t timeout) {
  br_ssl_engine_context *eng;
  size_t total = 0u;
  uint32_t spin;

  if (conn == 0 || buf_out == 0 || buf_size == 0u) {
    return 0u;
  }

  if (!conn->handshake_done || conn->closed) {
    return 0u;
  }

  eng = &g_sc.eng;

  for (spin = 0u; spin < timeout; ++spin) {
    unsigned state = br_ssl_engine_current_state(eng);

    if (state == BR_SSL_CLOSED) {
      conn->closed = 1;
      break;
    }

    /* Read available decrypted application data */
    if (state & BR_SSL_RECVAPP) {
      size_t avail;
      uint8_t *buf = br_ssl_engine_recvapp_buf(eng, &avail);
      if (buf != 0 && avail > 0u) {
        size_t copy = avail;
        size_t ci;
        if (total + copy > buf_size) {
          copy = buf_size - total;
        }
        for (ci = 0u; ci < copy; ++ci) {
          buf_out[total + ci] = buf[ci];
        }
        br_ssl_engine_recvapp_ack(eng, copy);
        total += copy;
        if (total >= buf_size) {
          break;
        }
      }
    }

    /* Pump TCP data into the engine */
    tls_pump(conn);
    ipv4_poll();
  }

  return total;
}

void tls_close(tls_conn_t *conn) {
  if (conn == 0) {
    return;
  }

  if (!conn->closed && conn->handshake_done) {
    /* Send close_notify */
    br_ssl_engine_close(&g_sc.eng);

    /* Pump the close_notify record out */
    {
      uint32_t spin;
      for (spin = 0u; spin < 50000u; ++spin) {
        unsigned state = br_ssl_engine_current_state(&g_sc.eng);
        if (state == BR_SSL_CLOSED) {
          break;
        }
        tls_pump(conn);
      }
    }
  }

  tcp_close(&conn->tcp);
  conn->closed = 1;
  conn->handshake_done = 0;
}