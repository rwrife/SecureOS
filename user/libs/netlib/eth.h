#ifndef SECUREOS_NET_ETH_H
#define SECUREOS_NET_ETH_H

/**
 * @file eth.h
 * @brief Ethernet frame types and layout constants.
 *
 * Purpose:
 *   Defines the Ethernet II frame header format and EtherType constants used
 *   by the SecureOS network stack.  Provides helpers for constructing and
 *   parsing raw Ethernet frames via the network HAL.
 *
 * Interactions:
 *   - network_hal.h: all frames are sent/received through network_hal_send/recv.
 *   - arp.c, ipv4.c: use eth_send_frame / eth_recv_frame to exchange packets.
 *
 * Launched by:
 *   Called from arp.c and ipv4.c as needed.  Not a standalone process;
 *   compiled into the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  ETH_HEADER_LEN = 14,
  ETH_MAC_LEN = 6,
  ETH_TYPE_ARP  = 0x0806,
  ETH_TYPE_IPV4 = 0x0800,
};

/* Broadcast MAC: ff:ff:ff:ff:ff:ff */
extern const uint8_t ETH_BROADCAST_MAC[ETH_MAC_LEN];

typedef struct {
  uint8_t dst[ETH_MAC_LEN];
  uint8_t src[ETH_MAC_LEN];
  uint16_t ethertype; /* big-endian */
} eth_header_t;

/* Build and send a frame; payload is placed after the header. */
int eth_send_frame(const uint8_t dst_mac[ETH_MAC_LEN],
                   uint16_t ethertype,
                   const uint8_t *payload,
                   size_t payload_len);

/* Receive next available frame; returns 1 on success, 0 if no frame available. */
int eth_recv_frame(uint8_t *frame_out, size_t frame_out_size, size_t *out_len);

/* Populate our MAC address (HAL-sourced). */
void eth_get_local_mac(uint8_t mac_out[ETH_MAC_LEN]);

#endif
