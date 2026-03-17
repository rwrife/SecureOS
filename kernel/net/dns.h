#ifndef SECUREOS_NET_DNS_H
#define SECUREOS_NET_DNS_H

/**
 * @file dns.h
 * @brief DNS A-record resolver interface.
 *
 * Purpose:
 *   Provides a blocking DNS A-record lookup that sends a UDP query to the
 *   configured DNS resolver (QEMU user-net 10.0.2.3) and polls for a reply.
 *   If the input is already a dotted-decimal IP string, it is parsed directly
 *   without a network query.
 *
 * Interactions:
 *   - udp.c: DNS queries are sent and received as UDP/53 datagrams.
 *   - http.c, tcp.c: call dns_resolve() to turn hostnames into IPv4 addresses.
 *
 * Launched by:
 *   Called on-demand from http.c when a hostname URL is requested.
 *   Not standalone; compiled into the kernel image.
 */

#include <stdint.h>

enum {
  DNS_PORT = 53,
  DNS_QUERY_MAX = 256,
  DNS_REPLY_MAX = 512,
  DNS_TIMEOUT_ITERATIONS = 6,
};

/* Resolve hostname to IPv4 address.
 * If hostname is already a dotted-decimal IP, parse and return it directly.
 * Returns resolved address in host byte order; returns 0 on failure. */
uint32_t dns_resolve(const char *hostname);

/* Convert dotted-decimal string to uint32_t (host byte order).
 * Returns 0 if the string is not a valid IPv4 literal. */
uint32_t dns_parse_ip_literal(const char *str);

#endif
