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
#include "../svc/broker_svc.h"
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

/* ---------------- M4 broker-demo entries (slice 3, issue #304) ----
 *
 * Decode the broker handle slot stamped by
 * `launcher_broker_spawn_app_with_broker_cap()` (#303). LE64 at
 * `ipc_scratch[24..32)`; upper 32 bits reserved-zero under
 * OS_ABI_VERSION=0, so we read the low 32 as the cap_handle_t.
 */
static cap_handle_t helloapp_scratch_load_broker_handle(const uint8_t *p) {
  return  (cap_handle_t)p[24]
       | ((cap_handle_t)p[25] <<  8)
       | ((cap_handle_t)p[26] << 16)
       | ((cap_handle_t)p[27] << 24);
}

static void helloapp_broker_zero_msg(ipc_msg_v0 *msg) {
  memset(msg, 0, sizeof(*msg));
  msg->abi_version = (uint16_t)OS_ABI_VERSION;
  msg->flags       = 0u;
}

/* Locate a scratch envelope buffer inside the spawned aspace's window
 * so the IPC bounds-check (issue #260) accepts it as caller-owned
 * memory. We place it well past the handoff region
 * (`ipc_scratch[0..64)`) at `aspace->base + IPC_MSG_PAYLOAD_MAX`,
 * which is inside the window — the launcher carves windows of size
 * `PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64` bytes minimum, so
 * the 80-byte envelope at offset 64 lies fully in-bounds.
 *
 * Returns NULL on degenerate aspace inputs. */
static ipc_msg_v0 *helloapp_broker_envelope_in_aspace(
    const address_space_t *aspace) {
  if (aspace == NULL || aspace->ipc_scratch == NULL) {
    return NULL;
  }
  /* ipc_scratch points at aspace->base; envelope sits just past the
   * 64-byte handoff scratch region. */
  return (ipc_msg_v0 *)(aspace->ipc_scratch + IPC_MSG_PAYLOAD_MAX);
}

/* Shared helper used by the approve / deny convenience entries: build
 * a tag-tagged envelope carrying a single LE32 share_id payload, send
 * via `broker_h` on `broker_port`, and emit the matching marker on
 * IPC_OK. Forward-declared because the request entry below also uses
 * it for the approve leg. */
static ipc_result_t helloapp_entry_broker_owner_send_sid_op(
    const address_space_t *aspace,
    ipc_port_t broker_port,
    broker_op_t op,
    cap_share_id_t share_id,
    const char *marker);

void helloapp_entry_broker_owner(const address_space_t *aspace,
                                 ipc_port_t broker_port,
                                 cap_subject_id_t recipient_subject,
                                 capability_id_t capability,
                                 const char *resource_name,
                                 size_t resource_name_len,
                                 const cap_share_id_t *share_id_in,
                                 helloapp_broker_owner_result_t *out) {
  if (out == NULL) {
    return;
  }
  out->broker_handle        = CAP_HANDLE_NULL;
  out->request_send_result  = IPC_ERR_INVALID_MSG;
  out->approve_send_result  = IPC_ERR_INVALID_MSG;

  if (aspace == NULL || aspace->ipc_scratch == NULL) {
    return;
  }
  /* Resource name must fit within the 31-byte slot in the request
   * payload schema. */
  if (resource_name_len > 31u ||
      (resource_name_len > 0u && resource_name == NULL)) {
    return;
  }

  const uint8_t *p = (const uint8_t *)aspace->ipc_scratch;
  cap_handle_t broker_h = helloapp_scratch_load_broker_handle(p);
  out->broker_handle = broker_h;

  ipc_msg_v0 *req = helloapp_broker_envelope_in_aspace(aspace);
  if (req == NULL) {
    return;
  }

  /* Leg 1: BROKER_OP_REQUEST. Payload schema in broker_svc.h. */
  helloapp_broker_zero_msg(req);
  req->tag = (uint32_t)BROKER_OP_REQUEST;
  req->payload_len = 40u;
  uint32_t recip32 = (uint32_t)recipient_subject;
  uint32_t cap32   = (uint32_t)capability;
  req->payload[0] = (uint8_t)(recip32      & 0xFFu);
  req->payload[1] = (uint8_t)((recip32 >> 8)  & 0xFFu);
  req->payload[2] = (uint8_t)((recip32 >> 16) & 0xFFu);
  req->payload[3] = (uint8_t)((recip32 >> 24) & 0xFFu);
  req->payload[4] = (uint8_t)(cap32      & 0xFFu);
  req->payload[5] = (uint8_t)((cap32 >> 8)  & 0xFFu);
  req->payload[6] = (uint8_t)((cap32 >> 16) & 0xFFu);
  req->payload[7] = (uint8_t)((cap32 >> 24) & 0xFFu);
  req->payload[8] = (uint8_t)resource_name_len;
  if (resource_name_len > 0u) {
    memcpy(&req->payload[9], resource_name, resource_name_len);
  }
  out->request_send_result = ipc_send_h(broker_h, broker_port, req);
  if (out->request_send_result == IPC_OK) {
    fputs("TEST:PASS:m4_broker_owner_qemu:request\n", stdout);
  } else {
    return; /* skip approve if request didn't even send */
  }

  /* Leg 2 (optional): BROKER_OP_APPROVE. Skipped when share_id_in is
   * NULL; callers that need fan-out symmetry use
   * `helloapp_entry_broker_owner_approve` (or `_deny`) directly after
   * the test driver drains the request envelope. */
  if (share_id_in == NULL) {
    out->approve_send_result = IPC_OK; /* not exercised; not a failure */
    return;
  }
  out->approve_send_result = helloapp_entry_broker_owner_send_sid_op(
      aspace, broker_port, BROKER_OP_APPROVE, *share_id_in,
      "TEST:PASS:m4_broker_owner_qemu:approve\n");
}

static ipc_result_t helloapp_entry_broker_owner_send_sid_op(
    const address_space_t *aspace,
    ipc_port_t broker_port,
    broker_op_t op,
    cap_share_id_t share_id,
    const char *marker) {
  if (aspace == NULL || aspace->ipc_scratch == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  const uint8_t *p = (const uint8_t *)aspace->ipc_scratch;
  cap_handle_t broker_h = helloapp_scratch_load_broker_handle(p);

  ipc_msg_v0 *msg = helloapp_broker_envelope_in_aspace(aspace);
  if (msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  helloapp_broker_zero_msg(msg);
  msg->tag = (uint32_t)op;
  msg->payload_len = 4u;
  uint32_t sid32 = (uint32_t)share_id;
  msg->payload[0] = (uint8_t)(sid32      & 0xFFu);
  msg->payload[1] = (uint8_t)((sid32 >> 8)  & 0xFFu);
  msg->payload[2] = (uint8_t)((sid32 >> 16) & 0xFFu);
  msg->payload[3] = (uint8_t)((sid32 >> 24) & 0xFFu);
  ipc_result_t rc = ipc_send_h(broker_h, broker_port, msg);
  if (rc == IPC_OK && marker != NULL) {
    fputs(marker, stdout);
  }
  return rc;
}

ipc_result_t helloapp_entry_broker_owner_approve(const address_space_t *aspace,
                                                 ipc_port_t broker_port,
                                                 cap_share_id_t share_id) {
  return helloapp_entry_broker_owner_send_sid_op(
      aspace, broker_port, BROKER_OP_APPROVE, share_id,
      "TEST:PASS:m4_broker_owner_qemu:approve\n");
}

ipc_result_t helloapp_entry_broker_owner_deny(const address_space_t *aspace,
                                              ipc_port_t broker_port,
                                              cap_share_id_t share_id) {
  return helloapp_entry_broker_owner_send_sid_op(
      aspace, broker_port, BROKER_OP_DENY, share_id,
      "TEST:PASS:m4_broker_owner_qemu:deny\n");
}
