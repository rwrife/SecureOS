#ifndef SECUREOS_NET_UDP_H
#define SECUREOS_NET_UDP_H

/**
 * @file udp.h
 * @brief UDP datagram send/receive interface.
 *
 * Purpose:
 *   Provides UDP datagram transmission and a single-slot receive buffer that
 *   is populated by ipv4_poll().  Used exclusively for DNS A-record queries.
 *
 * Interactions:
 *   - ipv4.c: udp_send() calls ipv4_send(); ipv4_poll() calls udp_dispatch()
 *     on arriving UDP packets.
 *   - dns.c: calls udp_send() to transmit DNS queries and udp_recv() to
 *     collect responses.
 *
 * Launched by:
 *   Called on-demand from dns.c.  Not standalone; compiled into the kernel.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  UDP_HEADER_LEN = 8,
  UDP_RECV_TIMEOUT = 600000, /* poll iterations before timeout */
};

/* Send a UDP datagram. */
int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *payload, size_t payload_len);

/* Receive next UDP datagram destined for src_port.
 * Blocks (polling) until data arrives or timeout.
 * Returns received payload length, or 0 on timeout. */
size_t udp_recv(uint16_t port, uint8_t *buf_out, size_t buf_size, uint32_t timeout);

/* Called by ipv4.c to dispatch an incoming UDP packet. */
void udp_dispatch(uint32_t src_ip, const uint8_t *udp_packet, size_t udp_len);

#endif
