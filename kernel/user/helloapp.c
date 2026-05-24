/**
 * @file helloapp.c
 * @brief HelloApp M2 substrate module body — slice 3 (#270).
 *
 * See `helloapp.h` for the contract and
 * `plans/2026-05-23-m2-on-m1-substrate.md` §"HelloApp module" for
 * design context. The body is deliberately tiny so the `_qemu`
 * validators can call it inline (no scheduler, no recv loop, no
 * console driver forwarding — those land in slice 4).
 *
 * Issue: #270.
 */

#include "helloapp.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../cap/cap_handle.h"
#include "../ipc/ipc_msg.h"
#include "../ipc/ipc_ops.h"
#include "../ipc/ipc_port.h"
#include "../proc/address_space.h"
#include "../../user/include/secureos_abi.h"

/*
 * Decode the launcher-handed-off `cap_handle_t` from the first four
 * bytes of `ipc_scratch` using the same little-endian convention as
 * `launcher.c::scratch_store_handle()` (slice 2, #269).
 *
 * Kept local to this TU rather than exported because the handoff
 * contract is owned by `docs/architecture/m1-m2-handoff.md`; if it
 * ever grows past four bytes the decoder lives next to the consumer.
 */
static cap_handle_t helloapp_scratch_load_handle(const address_space_t *as) {
  const uint8_t *p = (const uint8_t *)as->ipc_scratch;
  return  (cap_handle_t)p[0]
       | ((cap_handle_t)p[1] <<  8)
       | ((cap_handle_t)p[2] << 16)
       | ((cap_handle_t)p[3] << 24);
}

ipc_result_t helloapp_run_once(const address_space_t *aspace,
                               ipc_port_t console_port) {
  if (aspace == NULL || aspace->ipc_scratch == NULL) {
    /* No scratch means no handoff vector; treat as a malformed
     * invocation rather than a capability deny so the test surface
     * stays distinguishable from the real deny path. */
    return IPC_ERR_INVALID_MSG;
  }

  cap_handle_t h = helloapp_scratch_load_handle(aspace);

  /* Build the canonical envelope. Fields zeroed by `= {0}` then set
   * explicitly so any future field addition fails the build until it
   * is consciously handled (this is the same pattern used in
   * `tests/ipc_sync_v0_test.c`). */
  ipc_msg_v0 msg = {0};
  msg.abi_version    = (uint16_t)OS_ABI_VERSION;
  msg.flags          = 0u;
  msg.sender_subject = 0u;             /* kernel-stamped by ipc_send_h */
  msg.tag            = 0u;
  msg.payload_len    = (uint32_t)HELLOAPP_BANNER_LEN;
  memcpy(msg.payload, HELLOAPP_BANNER, HELLOAPP_BANNER_LEN);

  return ipc_send_h(h, console_port, &msg);
}

/* ---------------- M3 fs-demo entry (slice 3, issue #280) ----------
 *
 * Decode the wider fs-handle handoff layout (LE64 read at
 * ipc_scratch[8..16), LE64 write at ipc_scratch[16..24)) that
 * `launcher_fs_spawn_app_with_fs_caps` (#279) writes. The top 32
 * bits of each slot are reserved-zero under OS_ABI_VERSION=0, so we
 * truncate to the 32-bit cap_handle_t the rest of the kernel uses.
 */
static cap_handle_t helloapp_scratch_load_fs_handle(const uint8_t *p) {
  return  (cap_handle_t)p[0]
       | ((cap_handle_t)p[1] <<  8)
       | ((cap_handle_t)p[2] << 16)
       | ((cap_handle_t)p[3] << 24);
}

static void helloapp_fs_demo_build(ipc_msg_v0 *msg,
                                   const void *payload,
                                   size_t payload_len) {
  memset(msg, 0, sizeof(*msg));
  msg->abi_version = (uint16_t)OS_ABI_VERSION;
  msg->flags       = 0u;
  msg->payload_len = (uint32_t)payload_len;
  if (payload_len > 0u && payload != NULL) {
    memcpy(msg->payload, payload, payload_len);
  }
}

void helloapp_entry_fs_demo(const address_space_t *aspace,
                            ipc_port_t fs_read_port,
                            ipc_port_t fs_write_port,
                            helloapp_fs_demo_result_t *out) {
  if (out == NULL) {
    return;
  }
  out->write_send_result = IPC_ERR_INVALID_MSG;
  out->read_send_result  = IPC_ERR_INVALID_MSG;

  if (aspace == NULL || aspace->ipc_scratch == NULL) {
    return;
  }

  const uint8_t *p = (const uint8_t *)aspace->ipc_scratch;
  cap_handle_t read_h  = helloapp_scratch_load_fs_handle(&p[8]);
  cap_handle_t write_h = helloapp_scratch_load_fs_handle(&p[16]);

  /* Write leg: send the blob through CAP_FS_WRITE. Stamping the
   * envelope with the file path in `tag`/payload is the fs_svc loop's
   * job in a later slice; here the payload is the blob bytes the test
   * driver pins on the receive side. */
  ipc_msg_v0 write_req;
  helloapp_fs_demo_build(&write_req, HELLOAPP_FS_DEMO_BLOB,
                         HELLOAPP_FS_DEMO_BLOB_LEN);
  out->write_send_result = ipc_send_h(write_h, fs_write_port, &write_req);
  if (out->write_send_result == IPC_OK) {
    /* Per issue #280: one PASS marker per op on IPC_OK. */
    fputs("TEST:PASS:m3_helloapp_fs_qemu_op\n", stdout);
  }

  /* Read leg: send the path through CAP_FS_READ. */
  ipc_msg_v0 read_req;
  helloapp_fs_demo_build(&read_req, HELLOAPP_FS_DEMO_PATH,
                         HELLOAPP_FS_DEMO_PATH_LEN);
  out->read_send_result = ipc_send_h(read_h, fs_read_port, &read_req);
  if (out->read_send_result == IPC_OK) {
    fputs("TEST:PASS:m3_helloapp_fs_qemu_op\n", stdout);
  }
}
