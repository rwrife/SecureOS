#ifndef SECUREOS_NET_TCP_H
#define SECUREOS_NET_TCP_H

/**
 * @file tcp.h
 * @brief TCP client state and connection interface.
 *
 * Purpose:
 *   Provides a minimal blocking TCP client capable of performing a 3-way
 *   handshake, sending data, and receiving a bounded response.  All I/O is
 *   polling-based (no interrupts).  Intended for single short-lived HTTP
 *   connections.
 *
 * Interactions:
 *   - ipv4.c: tcp_send_raw() wraps IP-layer TX; incoming TCP packets arrive
 *     via ipv4_poll() → tcp_dispatch().
 *   - http.c: calls tcp_connect / tcp_send / tcp_recv / tcp_close.
 *
 * Launched by:
 *   Called on-demand from http.c per HTTP request.  Not standalone;
 *   compiled into the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  TCP_CONNECT_TIMEOUT = 500000,   /* poll iterations for SYN-ACK                  */
  TCP_RECV_TIMEOUT    = 1000000,  /* poll iterations for data after request sent   */
  TCP_EPHEMERAL_BASE  = 49152u,   /* starting ephemeral source port                */
  TCP_RESPONSE_MAX    = 4096,     /* max bytes buffered from a single response     */
};

typedef enum {
  TCP_OK = 0,
  TCP_ERR_CONNECT = 1,  /* failed to establish connection                */
  TCP_ERR_SEND    = 2,  /* failed to send data                           */
  TCP_ERR_RECV    = 3,  /* recv timeout or connection reset              */
  TCP_ERR_CLOSED  = 4,  /* server closed before response complete        */
} tcp_result_t;

typedef struct {
  uint32_t remote_ip;
  uint16_t remote_port;
  uint16_t local_port;
  uint32_t seq;
  uint32_t ack;
  int connected;
  int fin_received;
} tcp_conn_t;

/* Open a TCP connection. Returns TCP_OK on success. */
tcp_result_t tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port);

/* Send data on an established connection. */
tcp_result_t tcp_send(tcp_conn_t *conn, const uint8_t *data, size_t len);

/* Receive up to buf_size bytes into buf_out.
 * Blocks until response arrives or timeout.  Returns bytes received. */
size_t tcp_recv(tcp_conn_t *conn, uint8_t *buf_out, size_t buf_size, uint32_t timeout);

/* Send FIN and release connection state. */
void tcp_close(tcp_conn_t *conn);

/* Called by ipv4.c to dispatch an incoming TCP packet. */
void tcp_dispatch(uint32_t src_ip, const uint8_t *tcp_packet, size_t tcp_len,
                  tcp_conn_t *active_conn);

#endif
