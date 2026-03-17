/**
 * @file backend_kernel.c
 * @brief Kernel-side raw network backend for the shared netlib stack.
 *
 * Purpose:
 *   Bridges the relocated user/libs/netlib protocol sources to the kernel
 *   network HAL so the kernel can continue using the same implementation
 *   while the code physically lives in the shared netlib source tree.
 *
 * Interactions:
 *   - network_hal.c provides raw frame send/receive and device metadata.
 *   - The shared netlib protocol sources call the backend.h interface.
 *
 * Launched by:
 *   Compiled into the kernel image alongside the relocated netlib sources.
 */

#include "backend.h"

#include "../../../kernel/hal/network_hal.h"

int netlib_backend_ready(void) {
  return network_hal_ready() ? 1 : 0;
}

const char *netlib_backend_name(void) {
  return network_hal_backend_name();
}

void netlib_backend_get_mac(uint8_t mac_out[NETLIB_BACKEND_MAC_LEN]) {
  network_hal_get_mac(mac_out);
}

int netlib_backend_send(const uint8_t *frame, size_t frame_len) {
  return network_hal_send(frame, frame_len) == NETWORK_OK ? 1 : 0;
}

int netlib_backend_recv(uint8_t *frame_out, size_t frame_out_size, size_t *out_len) {
  return network_hal_recv(frame_out, frame_out_size, out_len) == NETWORK_OK ? 1 : 0;
}