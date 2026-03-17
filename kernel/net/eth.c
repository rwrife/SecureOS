/**
 * @file eth.c
 * @brief Ethernet frame construction and dispatch for SecureOS.
 *
 * Purpose:
 *   Wraps raw network HAL send/receive with Ethernet II framing.  Outbound
 *   packets are prefixed with a 14-byte header (dst, src, ethertype).
 *   Inbound frames are delivered as-is to callers such as arp.c and ipv4.c.
 *
 * Interactions:
 *   - network_hal.c: all packet I/O goes through network_hal_send/recv.
 *   - arp.c: calls eth_send_frame with EtherType 0x0806.
 *   - ipv4.c: calls eth_send_frame with EtherType 0x0800; polls via
 *     eth_recv_frame.
 *
 * Launched by:
 *   Called on-demand from arp.c and ipv4.c.  Not a standalone process;
 *   compiled into the kernel image.
 */

#include "eth.h"

#include <stddef.h>
#include <stdint.h>

#include "../hal/network_hal.h"

const uint8_t ETH_BROADCAST_MAC[ETH_MAC_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void eth_copy_mac(uint8_t dst[ETH_MAC_LEN], const uint8_t src[ETH_MAC_LEN]) {
  size_t i = 0u;
  for (i = 0u; i < (size_t)ETH_MAC_LEN; ++i) {
    dst[i] = src[i];
  }
}

/* Build big-endian 16-bit value into two bytes */
static void eth_put_be16(uint8_t *buf, uint16_t val) {
  buf[0] = (uint8_t)((val >> 8u) & 0xFFu);
  buf[1] = (uint8_t)(val & 0xFFu);
}

void eth_get_local_mac(uint8_t mac_out[ETH_MAC_LEN]) {
  network_hal_get_mac(mac_out);
}

int eth_send_frame(const uint8_t dst_mac[ETH_MAC_LEN],
                   uint16_t ethertype,
                   const uint8_t *payload,
                   size_t payload_len) {
  static uint8_t frame_buf[NETWORK_FRAME_MAX];
  uint8_t src_mac[ETH_MAC_LEN];
  size_t total = 0u;
  size_t i = 0u;

  if (payload == 0 && payload_len > 0u) {
    return 0;
  }
  if (ETH_HEADER_LEN + payload_len > sizeof(frame_buf)) {
    return 0;
  }

  eth_get_local_mac(src_mac);
  eth_copy_mac(frame_buf,              dst_mac);
  eth_copy_mac(frame_buf + ETH_MAC_LEN, src_mac);
  eth_put_be16(frame_buf + ETH_MAC_LEN * 2u, ethertype);

  for (i = 0u; i < payload_len; ++i) {
    frame_buf[ETH_HEADER_LEN + i] = payload[i];
  }

  total = ETH_HEADER_LEN + payload_len;
  /* Minimum Ethernet frame is 64 bytes (pad if needed) */
  if (total < 60u) {
    size_t j = 0u;
    for (j = total; j < 60u; ++j) {
      frame_buf[j] = 0u;
    }
    total = 60u;
  }

  return network_hal_send(frame_buf, total) == NETWORK_OK ? 1 : 0;
}

int eth_recv_frame(uint8_t *frame_out, size_t frame_out_size, size_t *out_len) {
  network_result_t result = 0;

  if (frame_out == 0 || out_len == 0) {
    return 0;
  }

  *out_len = 0u;
  result = network_hal_recv(frame_out, frame_out_size, out_len);
  return (result == NETWORK_OK && *out_len >= (size_t)ETH_HEADER_LEN) ? 1 : 0;
}
