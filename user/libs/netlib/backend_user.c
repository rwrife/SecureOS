/**
 * @file backend_user.c
 * @brief Shared-library raw network backend for the relocated netlib stack.
 *
 * Purpose:
 *   Bridges the relocated user/libs/netlib protocol sources to the SecureOS
 *   user ABI so the standalone netlib library contains the full networking
 *   stack and only depends on raw frame and device syscalls.
 *
 * Interactions:
 *   - secureos_api.h exposes os_net_device_* and os_net_frame_* ABI calls.
 *   - The shared netlib protocol sources call the backend.h interface.
 *
 * Launched by:
 *   Compiled into artifacts/lib/netlib.lib as part of the shared library.
 */

#include "backend.h"

#include "secureos_api.h"

int netlib_backend_ready(void) {
  return os_net_device_ready() == OS_STATUS_OK ? 1 : 0;
}

const char *netlib_backend_name(void) {
  static char name[16];

  if (os_net_device_backend(name, (unsigned int)sizeof(name)) != OS_STATUS_OK) {
    name[0] = '\0';
  }

  return name;
}

void netlib_backend_get_mac(uint8_t mac_out[NETLIB_BACKEND_MAC_LEN]) {
  size_t i = 0u;

  if (mac_out == 0) {
    return;
  }

  if (os_net_device_get_mac(mac_out, NETLIB_BACKEND_MAC_LEN) == OS_STATUS_OK) {
    return;
  }

  for (i = 0u; i < (size_t)NETLIB_BACKEND_MAC_LEN; ++i) {
    mac_out[i] = 0u;
  }
}

int netlib_backend_send(const uint8_t *frame, size_t frame_len) {
  return os_net_frame_send(frame, (unsigned int)frame_len) == OS_STATUS_OK ? 1 : 0;
}

int netlib_backend_recv(uint8_t *frame_out, size_t frame_out_size, size_t *out_len) {
  unsigned int frame_len = 0u;

  if (frame_out == 0 || out_len == 0) {
    return 0;
  }

  if (os_net_frame_recv(frame_out,
                        (unsigned int)frame_out_size,
                        &frame_len) != OS_STATUS_OK) {
    *out_len = 0u;
    return 0;
  }

  *out_len = (size_t)frame_len;
  return 1;
}