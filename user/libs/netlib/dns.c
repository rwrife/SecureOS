/**
 * @file dns.c
 * @brief DNS A-record resolver for the shared netlib stack.
 *
 * Purpose:
 *   Sends a minimal DNS A-record query over UDP port 53 to the QEMU user-net
 *   DNS resolver and parses the first A-record in the response. If the input
 *   string is already a dotted-decimal IPv4 literal it is parsed directly.
 *
 * Interactions:
 *   - udp.c sends and receives DNS datagrams.
 *   - http.c uses dns_resolve() for hostname-based URLs.
 *
 * Launched by:
 *   Called on-demand from http.c. Built into both the kernel and the
 *   standalone netlib shared library.
 */

#include "dns.h"

#include <stddef.h>
#include <stdint.h>

#include "udp.h"
#include "ipv4.h"

#define DNS_LOCAL_PORT 12053u

enum {
  DNS_FALLBACK_PUBLIC = 0x08080808u,
};

static size_t dns_strlen(const char *s) {
  size_t n = 0u;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static int dns_isdigit(char c) {
  return c >= '0' && c <= '9';
}

static uint16_t dns_get_be16(const uint8_t *buf, size_t off) {
  return (uint16_t)(((uint16_t)buf[off] << 8u) | buf[off + 1u]);
}

static uint32_t dns_get_be32(const uint8_t *buf, size_t off) {
  return ((uint32_t)buf[off] << 24u) | ((uint32_t)buf[off + 1u] << 16u) |
         ((uint32_t)buf[off + 2u] << 8u) | (uint32_t)buf[off + 3u];
}

static int dns_skip_name(const uint8_t *buf, size_t len, size_t *io_pos) {
  size_t pos = 0u;
  size_t guard = 0u;

  if (buf == 0 || io_pos == 0) {
    return 0;
  }

  pos = *io_pos;
  while (pos < len && guard < 64u) {
    uint8_t label_len = buf[pos];

    if (label_len == 0u) {
      pos += 1u;
      *io_pos = pos;
      return 1;
    }

    if ((label_len & 0xC0u) == 0xC0u) {
      if (pos + 1u >= len) {
        return 0;
      }
      pos += 2u;
      *io_pos = pos;
      return 1;
    }

    pos += (size_t)label_len + 1u;
    ++guard;
  }

  return 0;
}

uint32_t dns_parse_ip_literal(const char *str) {
  uint32_t result = 0u;
  int octet = 0;
  int count = 0;
  int val = 0;

  if (str == 0) {
    return 0u;
  }

  while (count < 4) {
    val = 0;
    if (!dns_isdigit(*str)) {
      return 0u;
    }
    while (dns_isdigit(*str)) {
      val = val * 10 + (*str - '0');
      ++str;
    }
    if (val > 255) {
      return 0u;
    }
    octet = val;
    result = (result << 8u) | (uint32_t)(unsigned int)octet;
    ++count;
    if (count < 4) {
      if (*str != '.') {
        return 0u;
      }
      ++str;
    }
  }

  return (*str == '\0') ? result : 0u;
}

static size_t dns_build_query(const char *hostname, uint8_t *buf_out,
                              size_t buf_size, uint16_t txid) {
  const char *cursor = hostname;
  size_t label_start = 0u;
  size_t label_len = 0u;
  size_t pos = 0u;

  if (buf_size < 12u) {
    return 0u;
  }

  buf_out[0] = (uint8_t)((txid >> 8u) & 0xFFu);
  buf_out[1] = (uint8_t)(txid & 0xFFu);
  buf_out[2] = 0x01u;
  buf_out[3] = 0x00u;
  buf_out[4] = 0x00u;
  buf_out[5] = 0x01u;
  buf_out[6] = 0x00u;
  buf_out[7] = 0x00u;
  buf_out[8] = 0x00u;
  buf_out[9] = 0x00u;
  buf_out[10] = 0x00u;
  buf_out[11] = 0x00u;
  pos = 12u;

  while (*cursor != '\0' && pos + 2u < buf_size) {
    label_start = pos;
    ++pos;
    label_len = 0u;
    while (*cursor != '\0' && *cursor != '.') {
      if (pos >= buf_size) {
        return 0u;
      }
      buf_out[pos++] = (uint8_t)*cursor;
      ++cursor;
      ++label_len;
    }
    buf_out[label_start] = (uint8_t)label_len;
    if (*cursor == '.') {
      ++cursor;
    }
  }

  if (pos + 5u > buf_size) {
    return 0u;
  }

  buf_out[pos++] = 0x00u;
  buf_out[pos++] = 0x00u;
  buf_out[pos++] = 0x01u;
  buf_out[pos++] = 0x00u;
  buf_out[pos++] = 0x01u;
  return pos;
}

static uint32_t dns_parse_response(const uint8_t *buf, size_t len, uint16_t txid) {
  uint16_t rx_txid = 0u;
  uint16_t flags = 0u;
  uint16_t qdcount = 0u;
  uint16_t ancount = 0u;
  size_t pos = 0u;
  uint16_t i = 0u;

  if (len < 12u) {
    return 0u;
  }

  rx_txid = (uint16_t)(((uint16_t)buf[0] << 8u) | buf[1]);
  if (rx_txid != txid) {
    return 0u;
  }

  flags = dns_get_be16(buf, 2u);
  qdcount = dns_get_be16(buf, 4u);
  ancount = dns_get_be16(buf, 6u);

  if ((flags & 0x8000u) == 0u) {
    return 0u;
  }
  if ((flags & 0x000Fu) != 0u) {
    return 0u;
  }
  if (ancount == 0u) {
    return 0u;
  }

  pos = 12u;

  for (i = 0u; i < qdcount; ++i) {
    if (!dns_skip_name(buf, len, &pos)) {
      return 0u;
    }
    if (pos + 4u > len) {
      return 0u;
    }
    pos += 4u;
  }

  for (i = 0u; i < ancount; ++i) {
    uint16_t rtype = 0u;
    uint16_t rclass = 0u;
    uint16_t rdlength = 0u;

    if (pos >= len) {
      return 0u;
    }

    if (!dns_skip_name(buf, len, &pos)) {
      return 0u;
    }

    if (pos + 10u > len) {
      return 0u;
    }

    rtype = dns_get_be16(buf, pos);
    rclass = dns_get_be16(buf, pos + 2u);
    pos += 4u;
    pos += 4u;
    rdlength = dns_get_be16(buf, pos);
    pos += 2u;

    if (rtype == 0x0001u && rclass == 0x0001u && rdlength == 4u && pos + 4u <= len) {
      return dns_get_be32(buf, pos);
    }

    if (pos + (size_t)rdlength > len) {
      return 0u;
    }
    pos += (size_t)rdlength;
  }

  return 0u;
}

uint32_t dns_resolve(const char *hostname) {
  static uint16_t g_txid = 0x1234u;
  uint8_t query_buf[DNS_QUERY_MAX];
  uint8_t reply_buf[DNS_REPLY_MAX];
  const uint32_t resolvers[2] = { NET_DNS_IP, DNS_FALLBACK_PUBLIC };
  size_t query_len = 0u;
  size_t reply_len = 0u;
  uint32_t resolved = 0u;
  size_t resolver_idx = 0u;
  int attempt = 0;

  if (hostname == 0 || hostname[0] == '\0') {
    return 0u;
  }

  resolved = dns_parse_ip_literal(hostname);
  if (resolved != 0u) {
    return resolved;
  }

  query_len = dns_build_query(hostname, query_buf, sizeof(query_buf), g_txid);
  if (query_len == 0u) {
    return 0u;
  }

  for (resolver_idx = 0u; resolver_idx < 2u && resolved == 0u; ++resolver_idx) {
    for (attempt = 0; attempt < DNS_TIMEOUT_ITERATIONS; ++attempt) {
      if (!udp_send(resolvers[resolver_idx], DNS_LOCAL_PORT, DNS_PORT, query_buf, query_len)) {
        continue;
      }

      reply_len = udp_recv(DNS_LOCAL_PORT,
                           reply_buf,
                           sizeof(reply_buf),
                           UDP_RECV_TIMEOUT);
      if (reply_len == 0u) {
        continue;
      }

      resolved = dns_parse_response(reply_buf, reply_len, g_txid);
      if (resolved != 0u) {
        break;
      }
    }
  }

  ++g_txid;
  if (g_txid == 0u) {
    g_txid = 1u;
  }

  return resolved;
}