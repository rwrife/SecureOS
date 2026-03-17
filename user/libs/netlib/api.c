/**
 * @file api.c
 * @brief Command-facing API layer for user-space netlib.
 *
 * Purpose:
 *   Implements the public netlib functions consumed by user commands. This
 *   layer builds high-level operations (ifconfig/http/ping) on top of the
 *   netlib protocol stack and raw backend abstractions.
 *
 * Interactions:
 *   - backend.h for device readiness, backend name, and MAC access.
 *   - ipv4.h for static guest/gateway/dns addressing constants.
 *   - dns.h, tcp.h, and http.h for protocol operations.
 *
 * Launched by:
 *   Linked into network-enabled user apps and the standalone netlib library.
 */

#include "lib/netlib.h"

#include <stddef.h>
#include <stdint.h>

#include "backend.h"
#include "dns.h"
#include "http.h"
#include "ipv4.h"
#include "tcp.h"

static size_t netlib_append_string(char *dst, size_t dst_size, size_t cursor, const char *src) {
  size_t i = 0u;

  if (dst == 0 || src == 0) {
    return cursor;
  }

  while (src[i] != '\0' && cursor + 1u < dst_size) {
    dst[cursor++] = src[i++];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static size_t netlib_append_u32_decimal(char *dst, size_t dst_size, size_t cursor, uint32_t value) {
  char digits[10];
  size_t count = 0u;
  size_t i = 0u;

  if (value == 0u) {
    if (cursor + 1u < dst_size) {
      dst[cursor++] = '0';
      dst[cursor] = '\0';
    }
    return cursor;
  }

  while (value > 0u && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10u));
    value /= 10u;
  }

  for (i = 0u; i < count; ++i) {
    if (cursor + 1u >= dst_size) {
      break;
    }
    dst[cursor++] = digits[count - i - 1u];
  }

  if (cursor < dst_size) {
    dst[cursor] = '\0';
  }
  return cursor;
}

static size_t netlib_append_hex_u8(char *dst, size_t dst_size, size_t cursor, uint8_t value) {
  static const char hex[] = "0123456789abcdef";

  if (cursor + 2u >= dst_size) {
    return cursor;
  }

  dst[cursor++] = hex[(value >> 4u) & 0x0Fu];
  dst[cursor++] = hex[value & 0x0Fu];
  dst[cursor] = '\0';
  return cursor;
}

static size_t netlib_append_ipv4(char *dst, size_t dst_size, size_t cursor, uint32_t ip_host_order) {
  cursor = netlib_append_u32_decimal(dst, dst_size, cursor, (ip_host_order >> 24u) & 0xFFu);
  cursor = netlib_append_string(dst, dst_size, cursor, ".");
  cursor = netlib_append_u32_decimal(dst, dst_size, cursor, (ip_host_order >> 16u) & 0xFFu);
  cursor = netlib_append_string(dst, dst_size, cursor, ".");
  cursor = netlib_append_u32_decimal(dst, dst_size, cursor, (ip_host_order >> 8u) & 0xFFu);
  cursor = netlib_append_string(dst, dst_size, cursor, ".");
  cursor = netlib_append_u32_decimal(dst, dst_size, cursor, ip_host_order & 0xFFu);
  return cursor;
}

netlib_status_t netlib_from_os_status(os_status_t status) {
  switch (status) {
    case OS_STATUS_OK:
      return NETLIB_STATUS_OK;
    case OS_STATUS_DENIED:
      return NETLIB_STATUS_DENIED;
    case OS_STATUS_NOT_FOUND:
      return NETLIB_STATUS_NOT_FOUND;
    default:
      return NETLIB_STATUS_ERROR;
  }
}

netlib_status_t netlib_device_ready(netlib_handle_t handle) {
  (void)handle;
  return netlib_backend_ready() ? NETLIB_STATUS_OK : NETLIB_STATUS_NOT_FOUND;
}

netlib_status_t netlib_get_interface_info(netlib_handle_t handle,
                                          netlib_interface_info_t *out_info) {
  netlib_status_t status;

  (void)handle;
  if (out_info == 0) {
    return NETLIB_STATUS_ERROR;
  }

  status = netlib_device_ready(NETLIB_HANDLE_INVALID);
  out_info->link_up = status == NETLIB_STATUS_OK ? 1 : 0;

  {
    const char *backend_name = netlib_backend_name();
    size_t i = 0u;

    while (backend_name != 0 && backend_name[i] != '\0' && i + 1u < sizeof(out_info->backend_name)) {
      out_info->backend_name[i] = backend_name[i];
      ++i;
    }
    out_info->backend_name[i] = '\0';
  }

  netlib_backend_get_mac(out_info->mac);
  return status;
}

netlib_status_t netlib_frame_send(netlib_handle_t handle,
                                  const unsigned char *frame,
                                  unsigned int frame_len) {
  (void)handle;
  if (frame == 0 || frame_len == 0u) {
    return NETLIB_STATUS_ERROR;
  }
  return netlib_backend_send(frame, (size_t)frame_len) ? NETLIB_STATUS_OK : NETLIB_STATUS_ERROR;
}

netlib_status_t netlib_frame_recv(netlib_handle_t handle,
                                  unsigned char *out_buffer,
                                  unsigned int out_buffer_size,
                                  unsigned int *out_frame_len) {
  size_t recv_len = 0u;

  (void)handle;
  if (out_buffer == 0 || out_buffer_size == 0u || out_frame_len == 0) {
    return NETLIB_STATUS_ERROR;
  }

  if (!netlib_backend_recv(out_buffer, (size_t)out_buffer_size, &recv_len)) {
    *out_frame_len = 0u;
    return NETLIB_STATUS_NOT_FOUND;
  }

  *out_frame_len = (unsigned int)recv_len;
  return NETLIB_STATUS_OK;
}

netlib_status_t netlib_ifconfig(netlib_handle_t handle,
                                char *out_buffer,
                                unsigned int out_buffer_size) {
  uint8_t mac[NETLIB_BACKEND_MAC_LEN];
  size_t cursor = 0u;
  size_t i = 0u;

  (void)handle;
  if (out_buffer == 0 || out_buffer_size == 0u) {
    return NETLIB_STATUS_ERROR;
  }

  out_buffer[0] = '\0';

  if (!netlib_backend_ready()) {
    cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "ifconfig\n");
    cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  link: down (no network backend)\n");
    (void)cursor;
    return NETLIB_STATUS_OK;
  }

  netlib_backend_get_mac(mac);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "ifconfig\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  link: up\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  backend: ");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, netlib_backend_name());
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  mac: ");
  for (i = 0u; i < NETLIB_BACKEND_MAC_LEN; ++i) {
    cursor = netlib_append_hex_u8(out_buffer, out_buffer_size, cursor, mac[i]);
    if (i + 1u < NETLIB_BACKEND_MAC_LEN) {
      cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, ":");
    }
  }
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  ipv4: ");
  cursor = netlib_append_ipv4(out_buffer, out_buffer_size, cursor, NET_GUEST_IP);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  gateway: ");
  cursor = netlib_append_ipv4(out_buffer, out_buffer_size, cursor, NET_GATEWAY_IP);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "\n");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "  dns: ");
  cursor = netlib_append_ipv4(out_buffer, out_buffer_size, cursor, NET_DNS_IP);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "\n");
  (void)cursor;

  return NETLIB_STATUS_OK;
}

netlib_status_t netlib_http_get(netlib_handle_t handle,
                                const char *url,
                                char *out_buffer,
                                unsigned int out_buffer_size) {
  http_request_t req;
  http_response_t resp;
  http_result_t result;
  size_t copy = 0u;
  size_t i = 0u;

  (void)handle;
  if (url == 0 || out_buffer == 0 || out_buffer_size == 0u) {
    return NETLIB_STATUS_ERROR;
  }

  req.method = "GET";
  req.url = url;
  req.extra_headers = 0;
  req.extra_header_count = 0u;
  req.body = 0;
  req.body_len = 0u;

  result = http_request(&req, &resp);
  if (result != HTTP_OK) {
    out_buffer[0] = '\0';
    return NETLIB_STATUS_ERROR;
  }

  copy = resp.body_len;
  if (copy + 1u > (size_t)out_buffer_size) {
    copy = (size_t)out_buffer_size - 1u;
  }

  for (i = 0u; i < copy; ++i) {
    out_buffer[i] = (char)resp.body[i];
  }
  out_buffer[copy] = '\0';
  return NETLIB_STATUS_OK;
}

netlib_status_t netlib_ping(netlib_handle_t handle,
                            const char *host,
                            char *out_buffer,
                            unsigned int out_buffer_size) {
  uint32_t dst_ip = 0u;
  tcp_conn_t conn;
  size_t cursor = 0u;

  (void)handle;
  if (host == 0 || out_buffer == 0 || out_buffer_size == 0u) {
    return NETLIB_STATUS_ERROR;
  }

  dst_ip = dns_resolve(host);
  if (dst_ip == 0u) {
    out_buffer[0] = '\0';
    return NETLIB_STATUS_ERROR;
  }

  if (tcp_connect(&conn, dst_ip, 80u) != TCP_OK) {
    out_buffer[0] = '\0';
    return NETLIB_STATUS_ERROR;
  }
  tcp_close(&conn);

  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, "pong from ");
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, host);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, " (");
  cursor = netlib_append_ipv4(out_buffer, out_buffer_size, cursor, dst_ip);
  cursor = netlib_append_string(out_buffer, out_buffer_size, cursor, ")\n");
  (void)cursor;
  return NETLIB_STATUS_OK;
}
