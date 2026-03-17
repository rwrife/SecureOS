/**
 * @file network_hal_test.c
 * @brief Tests for the network HAL singleton contract.
 *
 * Purpose:
 *   Validates primary registration, readiness, backend metadata,
 *   MAC retrieval, transmit/receive forwarding, and reset behavior for
 *   the network HAL abstraction.
 *
 * Interactions:
 *   - network_hal.c: exercises all public HAL APIs using a fake backend.
 *
 * Launched by:
 *   Intended to be compiled and run by the native test harness.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/hal/network_hal.h"

static uint8_t g_last_tx[NETWORK_FRAME_MAX];
static size_t g_last_tx_len = 0u;
static uint8_t g_rx_frame[NETWORK_FRAME_MAX];
static size_t g_rx_len = 0u;
static uint8_t g_mac[NETWORK_MAC_LEN] = {0x52u, 0x54u, 0x00u, 0x12u, 0x34u, 0x56u};

static void fail(const char *reason) {
  printf("TEST:FAIL:network_hal:%s\n", reason);
  exit(1);
}

static network_result_t fake_send_frame(const uint8_t *frame, size_t frame_len) {
  size_t i = 0u;
  g_last_tx_len = frame_len;
  for (i = 0u; i < frame_len && i < sizeof(g_last_tx); ++i) {
    g_last_tx[i] = frame[i];
  }
  return NETWORK_OK;
}

static network_result_t fake_recv_frame(uint8_t *frame_out, size_t frame_capacity, size_t *frame_len_out) {
  size_t i = 0u;

  if (g_rx_len == 0u) {
    return NETWORK_ERR_RX_EMPTY;
  }
  if (frame_capacity < g_rx_len) {
    return NETWORK_ERR_BUFFER_TOO_SMALL;
  }

  for (i = 0u; i < g_rx_len; ++i) {
    frame_out[i] = g_rx_frame[i];
  }
  *frame_len_out = g_rx_len;
  g_rx_len = 0u;
  return NETWORK_OK;
}

static void fake_get_mac(uint8_t mac_out[NETWORK_MAC_LEN]) {
  size_t i = 0u;
  for (i = 0u; i < NETWORK_MAC_LEN; ++i) {
    mac_out[i] = g_mac[i];
  }
}

int main(void) {
  static const network_device_t fake_device = {
      NETWORK_BACKEND_VIRTIO_NET,
      "virtio-net-test",
      fake_send_frame,
      fake_recv_frame,
      fake_get_mac,
  };
  uint8_t mac_out[NETWORK_MAC_LEN];
  uint8_t rx_out[NETWORK_FRAME_MAX];
  size_t rx_out_len = 0u;
  const uint8_t tx_frame[] = {0x01u, 0x02u, 0x03u, 0x04u};
  size_t i = 0u;

  printf("TEST:START:network_hal\n");

  network_hal_reset_for_tests();
  if (network_hal_ready()) {
    fail("reset_should_clear_ready_state");
  }
  if (network_hal_send(tx_frame, sizeof(tx_frame)) != NETWORK_ERR_NOT_READY) {
    fail("send_should_require_registration");
  }

  network_hal_register_primary(&fake_device);
  if (!network_hal_ready()) {
    fail("register_should_set_ready_state");
  }
  if (network_hal_backend() != NETWORK_BACKEND_VIRTIO_NET) {
    fail("backend_id_mismatch");
  }
  if (network_hal_backend_name() == 0) {
    fail("backend_name_missing");
  }

  network_hal_get_mac(mac_out);
  for (i = 0u; i < NETWORK_MAC_LEN; ++i) {
    if (mac_out[i] != g_mac[i]) {
      fail("get_mac_value_mismatch");
    }
  }

  if (network_hal_send(tx_frame, sizeof(tx_frame)) != NETWORK_OK) {
    fail("send_forward_failed");
  }
  if (g_last_tx_len != sizeof(tx_frame) || g_last_tx[0] != tx_frame[0]) {
    fail("send_forward_value_mismatch");
  }

  g_rx_frame[0] = 0xAAu;
  g_rx_frame[1] = 0xBBu;
  g_rx_frame[2] = 0xCCu;
  g_rx_len = 3u;
  if (network_hal_recv(rx_out, sizeof(rx_out), &rx_out_len) != NETWORK_OK) {
    fail("recv_forward_failed");
  }
  if (rx_out_len != 3u || rx_out[0] != 0xAAu || rx_out[2] != 0xCCu) {
    fail("recv_forward_value_mismatch");
  }

  network_hal_reset_for_tests();
  if (network_hal_ready()) {
    fail("reset_after_register_should_clear_ready_state");
  }

  printf("TEST:PASS:network_hal_contract\n");
  return 0;
}