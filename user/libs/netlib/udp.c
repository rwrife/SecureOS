/**
 * @file udp.c
 * @brief UDP datagram send/receive for the shared netlib stack.
 *
 * Purpose:
 *   Provides UDP datagram TX using ipv4_send() and a single-slot RX buffer
 *   populated by udp_dispatch() which is called from ipv4_poll(). Intended
 *   for DNS queries which are short-lived and single-threaded.
 *
 * Interactions:
 *   - ipv4.c wraps UDP datagrams and dispatches replies back here.
 *   - dns.c uses udp_send() for DNS queries and udp_recv() for responses.
 *
 * Launched by:
 *   Called on-demand. Built into both the kernel and the standalone netlib
 *   shared library.
 */

#include "udp.h"

#include <stddef.h>
#include <stdint.h>

#include "ipv4.h"

enum {
  UDP_RECV_BUF_SIZE = 512,
};

static uint8_t g_udp_recv_buf[UDP_RECV_BUF_SIZE];
static size_t g_udp_recv_len = 0u;
static uint16_t g_udp_recv_port = 0u;
static int g_udp_recv_ready = 0;

static void udp_put_be16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 1u] = (uint8_t)(val & 0xFFu);
}

static uint16_t udp_get_be16(const uint8_t *buf, size_t off) {
  return (uint16_t)(((uint16_t)buf[off] << 8u) | buf[off + 1u]);
}

int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
             const uint8_t *payload, size_t payload_len) {
  static uint8_t udp_buf[UDP_HEADER_LEN + 512];
  size_t total = 0u;
  size_t i = 0u;

  if (payload_len > sizeof(udp_buf) - UDP_HEADER_LEN) {
    return 0;
  }

  total = UDP_HEADER_LEN + payload_len;

  udp_put_be16(udp_buf, 0u, src_port);
  udp_put_be16(udp_buf, 2u, dst_port);
  udp_put_be16(udp_buf, 4u, (uint16_t)total);
  udp_buf[6] = 0u;
  udp_buf[7] = 0u;

  for (i = 0u; i < payload_len; ++i) {
    udp_buf[UDP_HEADER_LEN + i] = payload[i];
  }

  return ipv4_send(dst_ip, IPV4_PROTO_UDP, udp_buf, total);
}

size_t udp_recv(uint16_t port, uint8_t *buf_out, size_t buf_size, uint32_t timeout) {
  uint32_t spin = 0u;

  g_udp_recv_port = port;
  g_udp_recv_ready = 0;
  g_udp_recv_len = 0u;

  for (spin = 0u; spin < timeout && !g_udp_recv_ready; ++spin) {
    ipv4_poll();
  }

  if (!g_udp_recv_ready || g_udp_recv_len == 0u) {
    return 0u;
  }

  {
    size_t copy = g_udp_recv_len < buf_size ? g_udp_recv_len : buf_size;
    size_t i = 0u;
    for (i = 0u; i < copy; ++i) {
      buf_out[i] = g_udp_recv_buf[i];
    }
    g_udp_recv_ready = 0;
    return copy;
  }
}

void udp_dispatch(uint32_t src_ip, const uint8_t *udp_packet, size_t udp_len) {
  uint16_t dst_port = 0u;
  size_t payload_len = 0u;
  size_t copy = 0u;
  size_t i = 0u;

  (void)src_ip;

  if (udp_len < (size_t)UDP_HEADER_LEN) {
    return;
  }

  dst_port = udp_get_be16(udp_packet, 2u);
  payload_len = udp_len - (size_t)UDP_HEADER_LEN;

  if (dst_port != g_udp_recv_port) {
    return;
  }

  copy = payload_len < UDP_RECV_BUF_SIZE ? payload_len : UDP_RECV_BUF_SIZE;
  for (i = 0u; i < copy; ++i) {
    g_udp_recv_buf[i] = udp_packet[UDP_HEADER_LEN + i];
  }
  g_udp_recv_len = copy;
  g_udp_recv_ready = 1;
}