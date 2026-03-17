/**
 * @file native_net_service.c
 * @brief Native user-app networking service implementation.
 *
 * Purpose:
 *   Implements the kernel-side networking ABI bridge used by native user-app
 *   execution. This service is intentionally limited to raw device metadata
 *   and raw frame I/O so protocol logic stays in user-space netlib.
 *
 * Interactions:
 *   - network_hal.c for backend status, MAC, and frame I/O.
 *
 * Launched by:
 *   Invoked by process.c native bridge callbacks while a native app runs.
 */

#include "native_net_service.h"

#include <stdint.h>

#include "../hal/network_hal.h"

int native_net_device_ready(void) {
  return network_hal_ready() ? 0 : 2;
}

int native_net_device_backend(char *out_buffer, unsigned int out_buffer_size) {
  const char *name = network_hal_backend_name();
  unsigned int i = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 3;
  }

  while (name[i] != '\0' && i + 1u < out_buffer_size) {
    out_buffer[i] = name[i];
    ++i;
  }
  out_buffer[i] = '\0';
  return 0;
}

int native_net_device_get_mac(unsigned char *out_buffer, unsigned int out_buffer_size) {
  unsigned int i = 0u;
  uint8_t mac[NETWORK_MAC_LEN];

  if (out_buffer == 0 || out_buffer_size < NETWORK_MAC_LEN) {
    return 3;
  }

  network_hal_get_mac(mac);
  for (i = 0u; i < NETWORK_MAC_LEN; ++i) {
    out_buffer[i] = mac[i];
  }
  return 0;
}

int native_net_frame_send(const unsigned char *frame, unsigned int frame_len) {
  if (frame == 0 || frame_len == 0u) {
    return 3;
  }
  return network_hal_send(frame, (size_t)frame_len) == NETWORK_OK ? 0 : 3;
}

int native_net_frame_recv(unsigned char *out_buffer,
                          unsigned int out_buffer_size,
                          unsigned int *out_frame_len) {
  size_t len = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u || out_frame_len == 0) {
    return 3;
  }

  if (network_hal_recv(out_buffer, (size_t)out_buffer_size, &len) != NETWORK_OK) {
    *out_frame_len = 0u;
    return 2;
  }

  *out_frame_len = (unsigned int)len;
  return 0;
}
