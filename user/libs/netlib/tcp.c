/**
 * @file tcp.c
 * @brief Polling TCP client for the shared netlib stack.
 *
 * Purpose:
 *   Implements a minimal blocking TCP client: three-way handshake, data send,
 *   data receive until FIN or timeout, and graceful close. All I/O is
 *   polling-based and intended for single-connection HTTP transactions.
 *
 * Interactions:
 *   - ipv4.c sends and receives TCP segments.
 *   - http.c uses tcp_connect(), tcp_send(), tcp_recv(), and tcp_close().
 *
 * Launched by:
 *   Called on-demand from http.c. Built into both the kernel and the
 *   standalone netlib shared library.
 */

#include "tcp.h"

#include <stddef.h>
#include <stdint.h>

#include "backend.h"

#include "ipv4.h"

void ipv4_set_active_tcp_conn(tcp_conn_t *conn);

enum {
  TCP_FLAG_FIN = 0x01u,
  TCP_FLAG_SYN = 0x02u,
  TCP_FLAG_RST = 0x04u,
  TCP_FLAG_PSH = 0x08u,
  TCP_FLAG_ACK = 0x10u,
  TCP_HEADER_LEN = 20,
  TCP_WINDOW = 65535,
};

static uint16_t g_ephemeral_port = TCP_EPHEMERAL_BASE;
static uint8_t g_rx_ring[TCP_RESPONSE_MAX];
static size_t g_rx_head = 0u;
static size_t g_rx_tail = 0u;
static int g_conn_fin = 0;

static void tcp_put_be16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 1u] = (uint8_t)(val & 0xFFu);
}

static void tcp_put_be32(uint8_t *buf, size_t off, uint32_t val) {
  buf[off] = (uint8_t)((val >> 24u) & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 16u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 3u] = (uint8_t)(val & 0xFFu);
}

static uint16_t tcp_get_be16(const uint8_t *buf, size_t off) {
  return (uint16_t)(((uint16_t)buf[off] << 8u) | buf[off + 1u]);
}

static uint32_t tcp_get_be32(const uint8_t *buf, size_t off) {
  return ((uint32_t)buf[off] << 24u) | ((uint32_t)buf[off + 1u] << 16u) |
         ((uint32_t)buf[off + 2u] << 8u) | (uint32_t)buf[off + 3u];
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const uint8_t *tcp_seg, size_t seg_len) {
  uint32_t sum = 0u;
  size_t i = 0u;

  sum += (src_ip >> 16u) & 0xFFFFu;
  sum += src_ip & 0xFFFFu;
  sum += (dst_ip >> 16u) & 0xFFFFu;
  sum += dst_ip & 0xFFFFu;
  sum += (uint32_t)IPV4_PROTO_TCP;
  sum += (uint32_t)seg_len;

  for (i = 0u; i + 1u < seg_len; i += 2u) {
    sum += (uint32_t)((uint16_t)tcp_seg[i] << 8u) | tcp_seg[i + 1u];
  }
  if (seg_len & 1u) {
    sum += (uint32_t)tcp_seg[seg_len - 1u] << 8u;
  }

  while (sum >> 16u) {
    sum = (sum & 0xFFFFu) + (sum >> 16u);
  }
  return (uint16_t)(~sum);
}

static int tcp_send_segment(tcp_conn_t *conn, uint8_t flags,
                            const uint8_t *data, size_t data_len) {
  static uint8_t seg[TCP_HEADER_LEN + NETLIB_BACKEND_MTU];
  uint16_t csum = 0u;
  size_t i = 0u;

  if (data_len > (size_t)NETLIB_BACKEND_MTU) {
    data_len = (size_t)NETLIB_BACKEND_MTU;
  }

  tcp_put_be16(seg, 0u, conn->local_port);
  tcp_put_be16(seg, 2u, conn->remote_port);
  tcp_put_be32(seg, 4u, conn->seq);
  tcp_put_be32(seg, 8u, conn->ack);
  seg[12] = (uint8_t)((TCP_HEADER_LEN / 4u) << 4u);
  seg[13] = flags;
  tcp_put_be16(seg, 14u, (uint16_t)TCP_WINDOW);
  seg[16] = 0u;
  seg[17] = 0u;
  seg[18] = 0u;
  seg[19] = 0u;

  for (i = 0u; i < data_len; ++i) {
    seg[TCP_HEADER_LEN + i] = data[i];
  }

  csum = tcp_checksum(NET_GUEST_IP, conn->remote_ip, seg, TCP_HEADER_LEN + data_len);
  tcp_put_be16(seg, 16u, csum);

  return ipv4_send(conn->remote_ip, IPV4_PROTO_TCP, seg, TCP_HEADER_LEN + data_len);
}

void tcp_dispatch(uint32_t src_ip, const uint8_t *tcp_packet, size_t tcp_len,
                  tcp_conn_t *conn) {
  uint16_t src_port = 0u;
  uint16_t dst_port = 0u;
  uint32_t seq = 0u;
  uint32_t ack_num = 0u;
  uint8_t data_offset = 0u;
  uint8_t flags = 0u;
  size_t header_len = 0u;
  size_t payload_len = 0u;

  if (tcp_len < (size_t)TCP_HEADER_LEN || conn == 0) {
    return;
  }

  if (src_ip != conn->remote_ip) {
    return;
  }

  src_port = tcp_get_be16(tcp_packet, 0u);
  dst_port = tcp_get_be16(tcp_packet, 2u);
  seq = tcp_get_be32(tcp_packet, 4u);
  ack_num = tcp_get_be32(tcp_packet, 8u);
  data_offset = (uint8_t)((tcp_packet[12] >> 4u) * 4u);
  flags = tcp_packet[13];

  if (src_port != conn->remote_port || dst_port != conn->local_port) {
    return;
  }

  if ((flags & TCP_FLAG_RST) != 0u) {
    conn->connected = 0;
    conn->fin_received = 1;
    return;
  }

  if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
    conn->ack = seq + 1u;
    conn->seq = ack_num;
    conn->connected = 1;
    return;
  }

  if ((flags & TCP_FLAG_ACK) == 0u) {
    return;
  }

  header_len = (size_t)data_offset;
  if (header_len > tcp_len) {
    return;
  }
  payload_len = tcp_len - header_len;

  if (payload_len > 0u) {
    const uint8_t *data = tcp_packet + header_len;
    size_t i = 0u;

    conn->ack = seq + (uint32_t)payload_len;

    for (i = 0u; i < payload_len; ++i) {
      if (g_rx_tail < (size_t)TCP_RESPONSE_MAX) {
        g_rx_ring[g_rx_tail++] = data[i];
      }
    }

    tcp_send_segment(conn, TCP_FLAG_ACK, 0, 0u);
  }

  if ((flags & TCP_FLAG_FIN) != 0u) {
    conn->ack += 1u;
    conn->fin_received = 1;
    g_conn_fin = 1;
    tcp_send_segment(conn, TCP_FLAG_ACK, 0, 0u);
  }

  (void)ack_num;
}

tcp_result_t tcp_connect(tcp_conn_t *conn, uint32_t remote_ip, uint16_t remote_port) {
  uint32_t spin = 0u;

  if (conn == 0) {
    return TCP_ERR_CONNECT;
  }

  conn->remote_ip = remote_ip;
  conn->remote_port = remote_port;
  conn->local_port = g_ephemeral_port++;
  if (g_ephemeral_port == 0u) {
    g_ephemeral_port = TCP_EPHEMERAL_BASE;
  }
  conn->seq = 0xA1B2C3D4u;
  conn->ack = 0u;
  conn->connected = 0;
  conn->fin_received = 0;

  g_rx_head = 0u;
  g_rx_tail = 0u;
  g_conn_fin = 0;

  ipv4_set_active_tcp_conn(conn);

  if (!tcp_send_segment(conn, TCP_FLAG_SYN, 0, 0u)) {
    ipv4_set_active_tcp_conn(0);
    return TCP_ERR_CONNECT;
  }
  conn->seq += 1u;

  for (spin = 0u; spin < TCP_CONNECT_TIMEOUT && !conn->connected; ++spin) {
    ipv4_poll();
  }

  if (!conn->connected) {
    ipv4_set_active_tcp_conn(0);
    return TCP_ERR_CONNECT;
  }

  tcp_send_segment(conn, TCP_FLAG_ACK, 0, 0u);
  return TCP_OK;
}

tcp_result_t tcp_send(tcp_conn_t *conn, const uint8_t *data, size_t len) {
  size_t sent = 0u;

  if (conn == 0 || !conn->connected) {
    return TCP_ERR_SEND;
  }

  while (sent < len) {
    size_t chunk = len - sent;
    if (chunk > (size_t)NETLIB_BACKEND_MTU) {
      chunk = (size_t)NETLIB_BACKEND_MTU;
    }
    if (!tcp_send_segment(conn, TCP_FLAG_PSH | TCP_FLAG_ACK, data + sent, chunk)) {
      return TCP_ERR_SEND;
    }
    conn->seq += (uint32_t)chunk;
    sent += chunk;
  }

  return TCP_OK;
}

size_t tcp_recv(tcp_conn_t *conn, uint8_t *buf_out, size_t buf_size, uint32_t timeout) {
  uint32_t spin = 0u;

  if (conn == 0 || buf_out == 0) {
    return 0u;
  }

  for (spin = 0u; spin < timeout; ++spin) {
    ipv4_poll();
    if (g_conn_fin) {
      break;
    }
  }

  {
    size_t avail = g_rx_tail - g_rx_head;
    size_t copy = avail < buf_size ? avail : buf_size;
    size_t i = 0u;
    for (i = 0u; i < copy; ++i) {
      buf_out[i] = g_rx_ring[g_rx_head + i];
    }
    g_rx_head += copy;
    return copy;
  }
}

void tcp_close(tcp_conn_t *conn) {
  if (conn == 0 || !conn->connected) {
    ipv4_set_active_tcp_conn(0);
    return;
  }

  tcp_send_segment(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0u);
  conn->seq += 1u;
  conn->connected = 0;

  ipv4_set_active_tcp_conn(0);
}