#ifndef SECUREOS_NETWORK_HAL_H
#define SECUREOS_NETWORK_HAL_H

/**
 * @file network_hal.h
 * @brief Hardware Abstraction Layer for network devices.
 *
 * Purpose:
 *   Defines the uniform send/receive interface that decouples the TCP/IP
 *   network stack from concrete NIC drivers.  A NIC driver populates a
 *   network_device_t and registers it via network_hal_register_primary();
 *   all higher-level networking then calls through this abstraction.
 *
 * Interactions:
 *   - drivers/network/virtio_net.c: populates and registers the primary device.
 *   - kernel network sources: all packet TX/RX goes through
 *     network_hal_send/recv.
 *   - kmain.c: calls network_hal_ready() after drivers init to confirm NIC.
 *
 * Launched by:
 *   network_hal_register_primary() is called from virtio_net_init_primary()
 *   during kernel boot.  Not a standalone process; compiled into the kernel.
 */

#include <stddef.h>
#include <stdint.h>

typedef enum {
  NETWORK_OK = 0,
  NETWORK_ERR_NOT_READY = 1,
  NETWORK_ERR_BUFFER_INVALID = 2,
  NETWORK_ERR_BUFFER_TOO_SMALL = 3,
  NETWORK_ERR_TX_FULL = 4,
  NETWORK_ERR_RX_EMPTY = 5,
} network_result_t;

typedef enum {
  NETWORK_BACKEND_UNKNOWN = 0,
  NETWORK_BACKEND_VIRTIO_NET = 1,
} network_backend_t;

/* Maximum Ethernet frame size (header + 1500-byte payload + optional VLAN) */
enum {
  NETWORK_MTU = 1500,
  NETWORK_FRAME_MAX = 1518,
  NETWORK_MAC_LEN = 6,
};

typedef network_result_t (*network_send_fn_t)(const uint8_t *frame, size_t frame_len);
typedef network_result_t (*network_recv_fn_t)(uint8_t *frame_out, size_t frame_out_size, size_t *out_len);
typedef void (*network_get_mac_fn_t)(uint8_t mac_out[NETWORK_MAC_LEN]);

typedef struct {
  network_backend_t backend;
  const char *backend_name;
  network_send_fn_t send_frame;
  network_recv_fn_t recv_frame;
  network_get_mac_fn_t get_mac;
} network_device_t;

/* Registration and readiness */
void network_hal_reset_for_tests(void);
void network_hal_register_primary(const network_device_t *device);
int network_hal_ready(void);
network_backend_t network_hal_backend(void);
const char *network_hal_backend_name(void);
void network_hal_get_mac(uint8_t mac_out[NETWORK_MAC_LEN]);

/* Packet I/O */
network_result_t network_hal_send(const uint8_t *frame, size_t frame_len);
network_result_t network_hal_recv(uint8_t *frame_out, size_t frame_out_size, size_t *out_len);

#endif
