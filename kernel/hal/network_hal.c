/**
 * @file network_hal.c
 * @brief Hardware Abstraction Layer for network devices.
 *
 * Purpose:
 *   Holds a pointer to the single active network backend and routes all
 *   send/receive calls through it.  Mirrors the storage_hal pattern so
 *   upper layers are entirely independent of the physical NIC.
 *
 * Interactions:
 *   - drivers/network/virtio_net.c: registers itself as the primary device
 *     via network_hal_register_primary() at boot.
 *   - kernel network sources: packet I/O uses network_hal_send /
 *     network_hal_recv.
 *   - process.c: network_hal_backend_name() displayed by the "network" command.
 *
 * Launched by:
 *   network_hal_register_primary() is called from virtio_net_init_primary()
 *   during kernel startup.  Not a standalone process; compiled into the kernel.
 */

#include "network_hal.h"

static const network_device_t *network_primary_device;

void network_hal_reset_for_tests(void) {
  network_primary_device = 0;
}

void network_hal_register_primary(const network_device_t *device) {
  network_primary_device = device;
}

int network_hal_ready(void) {
  return network_primary_device != 0 &&
         network_primary_device->send_frame != 0 &&
         network_primary_device->recv_frame != 0;
}

network_backend_t network_hal_backend(void) {
  if (!network_hal_ready()) {
    return NETWORK_BACKEND_UNKNOWN;
  }
  return network_primary_device->backend;
}

const char *network_hal_backend_name(void) {
  if (!network_hal_ready() || network_primary_device->backend_name == 0) {
    return "unknown";
  }
  return network_primary_device->backend_name;
}

void network_hal_get_mac(uint8_t mac_out[NETWORK_MAC_LEN]) {
  size_t i = 0u;
  for (i = 0u; i < (size_t)NETWORK_MAC_LEN; ++i) {
    mac_out[i] = 0u;
  }
  if (!network_hal_ready() || network_primary_device->get_mac == 0) {
    return;
  }
  network_primary_device->get_mac(mac_out);
}

network_result_t network_hal_send(const uint8_t *frame, size_t frame_len) {
  if (frame == 0) {
    return NETWORK_ERR_BUFFER_INVALID;
  }
  if (!network_hal_ready()) {
    return NETWORK_ERR_NOT_READY;
  }
  return network_primary_device->send_frame(frame, frame_len);
}

network_result_t network_hal_recv(uint8_t *frame_out, size_t frame_out_size, size_t *out_len) {
  if (frame_out == 0 || out_len == 0) {
    return NETWORK_ERR_BUFFER_INVALID;
  }
  if (!network_hal_ready()) {
    return NETWORK_ERR_NOT_READY;
  }
  return network_primary_device->recv_frame(frame_out, frame_out_size, out_len);
}
