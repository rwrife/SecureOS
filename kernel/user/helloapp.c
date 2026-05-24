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
