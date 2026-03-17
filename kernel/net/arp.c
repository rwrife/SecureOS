/**
 * @file arp.c
 * @brief ARP request/reply and address cache for SecureOS.
 *
 * Purpose:
 *   Maintains a small cache of IPv4-to-MAC mappings.  Resolves an IPv4
 *   address by probing the cache first, then sending an ARP request and
 *   polling for a reply.  Incoming ARP replies are processed by
 *   arp_process_packet() which is called from ipv4_poll().
 *
 * Interactions:
 *   - eth.c: ARP packets are sent as Ethernet frames with EtherType 0x0806.
 *   - ipv4.c: calls arp_resolve() before building each IP packet; forwards
 *     received ARP frames to arp_process_packet().
 *
 * Launched by:
 *   Called on-demand.  Not a standalone process; compiled into the kernel.
 */

#include "arp.h"

#include <stddef.h>
#include <stdint.h>

#include "eth.h"
#include "ipv4.h"

/* ARP packet layout constants */
enum {
  ARP_HW_ETHERNET   = 1,
  ARP_PROTO_IPV4    = 0x0800,
  ARP_OP_REQUEST    = 1,
  ARP_OP_REPLY      = 2,
  ARP_PACKET_LEN    = 28,  /* header(8) + 2*MAC(6) + 2*IP(4) */
};

typedef struct {
  uint32_t ip;
  uint8_t  mac[6];
  int      valid;
} arp_entry_t;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

/* ARP reply reception slot – populated by arp_process_packet() */
static uint32_t g_pending_ip = 0u;
static uint8_t  g_pending_mac[6];
static int      g_pending_resolved = 0;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static uint16_t arp_get_be16(const uint8_t *buf, size_t off) {
  return (uint16_t)(((uint16_t)buf[off] << 8u) | buf[off + 1u]);
}

static uint32_t arp_get_be32(const uint8_t *buf, size_t off) {
  return ((uint32_t)buf[off] << 24u) | ((uint32_t)buf[off + 1u] << 16u) |
         ((uint32_t)buf[off + 2u] << 8u) | (uint32_t)buf[off + 3u];
}

static void arp_put_be16(uint8_t *buf, size_t off, uint16_t val) {
  buf[off]     = (uint8_t)((val >> 8u) & 0xFFu);
  buf[off + 1u] = (uint8_t)(val & 0xFFu);
}

static void arp_put_be32(uint8_t *buf, size_t off, uint32_t val) {
  buf[off]     = (uint8_t)((val >> 24u) & 0xFFu);
  buf[off + 1u] = (uint8_t)((val >> 16u) & 0xFFu);
  buf[off + 2u] = (uint8_t)((val >> 8u)  & 0xFFu);
  buf[off + 3u] = (uint8_t)(val & 0xFFu);
}

static void arp_copy_mac(uint8_t dst[6], const uint8_t src[6]) {
  size_t i = 0u;
  for (i = 0u; i < 6u; ++i) {
    dst[i] = src[i];
  }
}

static int arp_mac_equals(const uint8_t a[6], const uint8_t b[6]) {
  size_t i = 0u;
  for (i = 0u; i < 6u; ++i) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

/* -----------------------------------------------------------------------
 * Cache operations
 * --------------------------------------------------------------------- */

void arp_cache_insert(uint32_t ip, const uint8_t mac[6]) {
  int evict_idx = 0;
  size_t i = 0u;

  /* Update existing entry if present */
  for (i = 0u; i < (size_t)ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
      arp_copy_mac(g_arp_cache[i].mac, mac);
      return;
    }
  }

  /* Find a free slot */
  for (i = 0u; i < (size_t)ARP_CACHE_SIZE; ++i) {
    if (!g_arp_cache[i].valid) {
      g_arp_cache[i].ip    = ip;
      g_arp_cache[i].valid = 1;
      arp_copy_mac(g_arp_cache[i].mac, mac);
      return;
    }
  }

  /* Evict slot 0 (simple FIFO policy) */
  g_arp_cache[evict_idx].ip    = ip;
  g_arp_cache[evict_idx].valid = 1;
  arp_copy_mac(g_arp_cache[evict_idx].mac, mac);
}

static int arp_cache_lookup(uint32_t ip, uint8_t mac_out[6]) {
  size_t i = 0u;
  for (i = 0u; i < (size_t)ARP_CACHE_SIZE; ++i) {
    if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
      arp_copy_mac(mac_out, g_arp_cache[i].mac);
      return 1;
    }
  }
  return 0;
}

/* -----------------------------------------------------------------------
 * ARP packet processing (called from ipv4_poll on EtherType 0x0806)
 * --------------------------------------------------------------------- */

void arp_process_packet(const uint8_t *payload, size_t payload_len) {
  uint16_t hw_type = 0u, proto_type = 0u, op = 0u;
  uint8_t sender_mac[6];
  uint32_t sender_ip = 0u;

  if (payload_len < (size_t)ARP_PACKET_LEN) {
    return;
  }

  hw_type    = arp_get_be16(payload, 0u);
  proto_type = arp_get_be16(payload, 2u);
  op         = arp_get_be16(payload, 6u);

  if (hw_type != ARP_HW_ETHERNET || proto_type != ARP_PROTO_IPV4) {
    return;
  }

  /* Sender hardware address at offset 8, sender IP at offset 14 */
  arp_copy_mac(sender_mac, payload + 8u);
  sender_ip = arp_get_be32(payload, 14u);

  /* Update cache unconditionally */
  arp_cache_insert(sender_ip, sender_mac);

  /* Notify pending resolve */
  if (op == ARP_OP_REPLY && sender_ip == g_pending_ip) {
    arp_copy_mac(g_pending_mac, sender_mac);
    g_pending_resolved = 1;
  }
  (void)arp_mac_equals; /* suppress unused warning */
}

/* -----------------------------------------------------------------------
 * Public: resolve IPv4 → MAC (request + poll)
 * --------------------------------------------------------------------- */

int arp_resolve(uint32_t target_ip, uint8_t mac_out[6]) {
  uint8_t local_mac[6];
  uint8_t pkt[ARP_PACKET_LEN];
  int attempt = 0;

  /* Fast path: already cached */
  if (arp_cache_lookup(target_ip, mac_out)) {
    return 1;
  }

  eth_get_local_mac(local_mac);

  for (attempt = 0; attempt < ARP_RETRIES; ++attempt) {
    uint32_t spin = 0u;

    /* Build ARP request */
    arp_put_be16(pkt, 0u, ARP_HW_ETHERNET);
    arp_put_be16(pkt, 2u, ARP_PROTO_IPV4);
    pkt[4] = 6u;  /* hw addr len */
    pkt[5] = 4u;  /* proto addr len */
    arp_put_be16(pkt, 6u, ARP_OP_REQUEST);
    arp_copy_mac(pkt + 8u, local_mac);
    arp_put_be32(pkt, 14u, NET_GUEST_IP);
    /* Target MAC: zeros for ARP request */
    pkt[18] = 0u; pkt[19] = 0u; pkt[20] = 0u;
    pkt[21] = 0u; pkt[22] = 0u; pkt[23] = 0u;
    arp_put_be32(pkt, 24u, target_ip);

    eth_send_frame(ETH_BROADCAST_MAC, ETH_TYPE_ARP, pkt, ARP_PACKET_LEN);

    g_pending_ip       = target_ip;
    g_pending_resolved = 0;

    /* Poll for reply */
    for (spin = 0u; spin < 50000u && !g_pending_resolved; ++spin) {
      ipv4_poll();
    }

    if (g_pending_resolved) {
      arp_copy_mac(mac_out, g_pending_mac);
      return 1;
    }
  }

  return 0;
}
