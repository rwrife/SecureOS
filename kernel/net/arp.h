#ifndef SECUREOS_NET_ARP_H
#define SECUREOS_NET_ARP_H

/**
 * @file arp.h
 * @brief ARP protocol types and cache interface.
 *
 * Purpose:
 *   Defines ARP packet layout and provides an ARP cache for resolving IPv4
 *   addresses to Ethernet MAC addresses.  Used by ipv4.c before sending any
 *   IP packet to determine the next-hop MAC address.
 *
 * Interactions:
 *   - ipv4.c: calls arp_resolve() before building an IP packet.
 *   - eth.c: ARP packets are sent/received as Ethernet frames.
 *
 * Launched by:
 *   arp_resolve() is called on-demand from ipv4_send(). Not standalone;
 *   compiled into the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

enum {
  ARP_CACHE_SIZE = 8,
  ARP_RETRIES = 3,
};

/* Resolve an IPv4 address to a MAC address.
 * Sends an ARP request and polls for reply; returns 1 on success. */
int arp_resolve(uint32_t target_ip, uint8_t mac_out[6]);

/* Inject a known ARP mapping (used at init to seed gateway). */
void arp_cache_insert(uint32_t ip, const uint8_t mac[6]);

/* Process an incoming ARP packet (should be called from eth receive loop). */
void arp_process_packet(const uint8_t *payload, size_t payload_len);

#endif
