/**
 * @file ipv4.c
 * @brief IPv4 packet construction, routing, and receive dispatch.
 *
 * Purpose:
 *   Builds IPv4 headers with correct checksums, resolves the next-hop MAC
 *   via ARP, and wraps IP payloads in Ethernet frames.  The receive path
 *   polls the network HAL for incoming Ethernet frames and dispatches them
 *   to ARP or the appropriate upper-layer handler (UDP or TCP).
 *
 * Interactions:
 *   - eth.c: IP packets are sent/received as Ethernet frames.
 *   - arp.c: MAC resolution before TX; ARP incoming frames routed here.
 *   - udp.c: udp_dispatch() called for PROTO=17 packets.
 *   - tcp.c: tcp_dispatch() called for PROTO=6 packets.
 *
 * Launched by:
 *   Called on-demand from udp.c, tcp.c, and the ARP probe loop.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "ipv4.h"

#include <stddef.h>
#include <stdint.h>

#include "../hal/network_hal.h"

#include "eth.h"
#include "arp.h"
#include "udp.h"
#include "tcp.h"

/* Active TCP connection pointer for dispatch (set when tcp_connect() is active) */
static tcp_conn_t *g_active_tcp_conn = 0;

/* Set by tcp.c before polling – package-private linkage via declaration below */
void ipv4_set_active_tcp_conn(tcp_conn_t *conn) {
  g_active_tcp_conn = conn;
}

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static void ipv4_put_be16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off]      = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 1u] = (uint8_t)(val & 0xFFu);
}

static void ipv4_put_be32(uint8_t *buf, size_t off, uint32_t val) {
  buf[off]      = (uint8_t)((val >> 24u) & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 16u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((val >> 8u)  & 0xFFu);
  buf[off + 3u] = (uint8_t)(val & 0xFFu);
}

static uint16_t ipv4_get_be16(const uint8_t *buf, size_t off) {
  return (uint16_t)(((uint16_t)buf[off] << 8u) | buf[off + 1u]);
}

static uint32_t ipv4_get_be32(const uint8_t *buf, size_t off) {
  return ((uint32_t)buf[off] << 24u) | ((uint32_t)buf[off + 1u] << 16u) |
         ((uint32_t)buf[off + 2u] << 8u) | (uint32_t)buf[off + 3u];
}

static uint16_t g_ip_id = 0u;

/* -----------------------------------------------------------------------
 * Public: checksum
 * --------------------------------------------------------------------- */

uint16_t ipv4_checksum(const uint8_t *header, size_t len) {
  uint32_t sum = 0u;
  size_t i = 0u;

  for (i = 0u; i + 1u < len; i += 2u) {
    sum += (uint32_t)((uint16_t)header[i] << 8u) | header[i + 1u];
  }
  if (len & 1u) {
    sum += (uint32_t)header[len - 1u] << 8u;
  }
  while (sum >> 16u) {
    sum = (sum & 0xFFFFu) + (sum >> 16u);
  }
  return (uint16_t)(~sum);
}

/* -----------------------------------------------------------------------
 * Public: send
 * --------------------------------------------------------------------- */

int ipv4_send(uint32_t dst_ip, uint8_t proto,
              const uint8_t *payload, size_t payload_len) {
  static uint8_t pkt_buf[IPV4_HEADER_LEN + NETWORK_MTU];
  uint8_t dst_mac[6];
  uint32_t next_hop = 0u;
  size_t total = 0u;
  size_t i = 0u;

  if (payload_len > (size_t)NETWORK_MTU) {
    return 0;
  }

  /*
   * QEMU user-mode networking (slirp) expects outbound traffic to flow via
   * the synthetic gateway.  Route all non-self packets through 10.0.2.2 so
   * services like DNS (10.0.2.3) are reachable even if they do not answer
   * ARP as standalone hosts.
   */
  if (dst_ip == NET_GUEST_IP) {
    next_hop = dst_ip;
  } else {
    next_hop = NET_GATEWAY_IP;
  }

  if (!arp_resolve(next_hop, dst_mac)) {
    return 0;
  }

  total = IPV4_HEADER_LEN + payload_len;

  /* Build IPv4 header */
  pkt_buf[0] = 0x45u;   /* version=4, IHL=5 */
  pkt_buf[1] = 0u;      /* DSCP/ECN */
  ipv4_put_be16(pkt_buf, 2u, (uint16_t)total);
  ipv4_put_be16(pkt_buf, 4u, g_ip_id++);
  pkt_buf[6] = 0u;       /* flags */
  pkt_buf[7] = 0u;       /* fragment offset */
  pkt_buf[8] = 64u;      /* TTL */
  pkt_buf[9] = proto;
  pkt_buf[10] = 0u;      /* checksum placeholder (high) */
  pkt_buf[11] = 0u;      /* checksum placeholder (low) */
  ipv4_put_be32(pkt_buf, 12u, NET_GUEST_IP);
  ipv4_put_be32(pkt_buf, 16u, dst_ip);

  /* Checksum over the 20-byte header */
  {
    uint16_t csum = ipv4_checksum(pkt_buf, IPV4_HEADER_LEN);
    ipv4_put_be16(pkt_buf, 10u, csum);
  }

  for (i = 0u; i < payload_len; ++i) {
    pkt_buf[IPV4_HEADER_LEN + i] = payload[i];
  }

  return eth_send_frame(dst_mac, ETH_TYPE_IPV4, pkt_buf, total);
}

/* -----------------------------------------------------------------------
 * Public: poll (receive and dispatch one frame)
 * --------------------------------------------------------------------- */

int ipv4_poll(void) {
  static uint8_t frame_buf[NETWORK_FRAME_MAX];
  size_t frame_len = 0u;
  uint16_t ethertype = 0u;
  const uint8_t *payload = 0;
  size_t payload_len = 0u;

  if (!eth_recv_frame(frame_buf, sizeof(frame_buf), &frame_len)) {
    return 0;
  }

  if (frame_len < (size_t)ETH_HEADER_LEN) {
    return 0;
  }

  /* EtherType: big-endian at bytes 12-13 */
  ethertype = (uint16_t)(((uint16_t)frame_buf[12] << 8u) | frame_buf[13]);
  payload     = frame_buf + ETH_HEADER_LEN;
  payload_len = frame_len - ETH_HEADER_LEN;

  if (ethertype == ETH_TYPE_ARP) {
    arp_process_packet(payload, payload_len);
    return 1;
  }

  if (ethertype != ETH_TYPE_IPV4) {
    return 0;
  }

  /* Minimal IPv4 validation */
  if (payload_len < (size_t)IPV4_HEADER_LEN) {
    return 0;
  }
  if ((payload[0] >> 4u) != 4u) {
    return 0;
  }

  {
    uint8_t  ihl        = (uint8_t)((payload[0] & 0x0Fu) * 4u);
    uint8_t  proto      = payload[9];
    uint32_t dst_ip     = ipv4_get_be32(payload, 16u);
    uint32_t src_ip     = ipv4_get_be32(payload, 12u);
    const uint8_t *data = payload + ihl;
    size_t   data_len   = 0u;

    if (ihl < (uint8_t)IPV4_HEADER_LEN || (size_t)ihl > payload_len) {
      return 0;
    }
    data_len = payload_len - (size_t)ihl;

    /* Only process packets destined for us */
    if (dst_ip != NET_GUEST_IP) {
      return 0;
    }

    if (proto == IPV4_PROTO_UDP) {
      udp_dispatch(src_ip, data, data_len);
    } else if (proto == IPV4_PROTO_TCP && g_active_tcp_conn != 0) {
      tcp_dispatch(src_ip, data, data_len, g_active_tcp_conn);
    }
  }

  return 1;
}
