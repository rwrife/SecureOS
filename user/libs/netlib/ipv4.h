#ifndef SECUREOS_NET_IPV4_H
#define SECUREOS_NET_IPV4_H

/**
 * @file ipv4.h
 * @brief IPv4 packet types and send/receive interface.
 *
 * Purpose:
 *   Provides IPv4 packet construction, checksum computation, basic routing
 *   (direct host or default gateway), and receive dispatch.  Supports UDP
 *   and TCP as upper layers.
 *
 * Interactions:
 *   - eth.c: IP packets are wrapped in Ethernet frames.
 *   - arp.c: MAC address resolved for each destination before TX.
 *   - udp.c, tcp.c: call ipv4_send(); incoming frames dispatched upward.
 *
 * Launched by:
 *   Called on-demand from udp.c and tcp.c.  Not standalone; compiled into
 *   the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  IPV4_PROTO_ICMP = 1,
  IPV4_PROTO_TCP  = 6,
  IPV4_PROTO_UDP  = 17,
  IPV4_HEADER_LEN = 20,
};

/* Guest networking constants (QEMU user-mode defaults; no DHCP). */
#define NET_GUEST_IP   0x0A00020Fu  /* 10.0.2.15 */
#define NET_GATEWAY_IP 0x0A000202u  /* 10.0.2.2  */
#define NET_DNS_IP     0x0A000203u  /* 10.0.2.3  */

/* Send an IPv4 packet.  Resolves MAC via ARP.
 * Returns 1 on success, 0 on failure. */
int ipv4_send(uint32_t dst_ip, uint8_t proto,
              const uint8_t *payload, size_t payload_len);

/* Poll: receive any pending Ethernet frame and dispatch to ARP/UDP/TCP.
 * Returns 1 if a frame was processed, 0 otherwise. */
int ipv4_poll(void);

/* Compute IP header checksum for a 20-byte header buffer. */
uint16_t ipv4_checksum(const uint8_t *header, size_t len);

#endif
