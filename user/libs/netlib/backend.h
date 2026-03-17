/**
 * @file backend.h
 * @brief Raw network-device backend contract for the shared netlib stack.
 *
 * Purpose:
 *   Defines the narrow send/receive and device metadata hooks that the
 *   relocated netlib protocol stack uses instead of depending directly on
 *   kernel HAL symbols. Kernel builds provide a HAL-backed implementation,
 *   while the shared-library build uses SecureOS ABI wrappers.
 *
 * Interactions:
 *   - backend_kernel.c adapts these calls to network_hal.c.
 *   - backend_user.c adapts these calls to secureos_api.h.
 *   - eth.c, ipv4.c, and tcp.c use these helpers for raw frame access.
 *
 * Launched by:
 *   Included by the shared netlib protocol sources. Not a standalone module.
 */

#ifndef SECUREOS_USER_NETLIB_BACKEND_H
#define SECUREOS_USER_NETLIB_BACKEND_H

#include <stddef.h>
#include <stdint.h>

enum {
  NETLIB_BACKEND_MTU = 1500,
  NETLIB_BACKEND_FRAME_MAX = 1518,
  NETLIB_BACKEND_MAC_LEN = 6,
};

int netlib_backend_ready(void);
const char *netlib_backend_name(void);
void netlib_backend_get_mac(uint8_t mac_out[NETLIB_BACKEND_MAC_LEN]);
int netlib_backend_send(const uint8_t *frame, size_t frame_len);
int netlib_backend_recv(uint8_t *frame_out, size_t frame_out_size, size_t *out_len);

#endif