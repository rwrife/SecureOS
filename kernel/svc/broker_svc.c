/**
 * @file broker_svc.c
 * @brief M4-on-M1 capability-broker service implementation — slice 1.
 *
 * See `broker_svc.h` for the public contract and
 * `plans/2026-05-25-m4-broker-on-m1-substrate.md` §"Broker service
 * module" for the design context.
 *
 * Slice scope (intentionally narrow — issue #302):
 *   - Allocate one well-known port at init.
 *   - Expose the handle for downstream slices (#303, #304, #305).
 *   - Do NOT spin a recv loop or dispatch broker request/approve/
 *     deny/revoke envelopes yet — those land with the acceptance
 *     peers (#304 / #305), together with the `BROKER_OP_*` tag enum
 *     and the bounded `share_id -> cap_handle_t` side-table.
 *
 * Boot-order edge:
 *   `broker_svc_init()` MUST run AFTER `ipc_port_table_init()` (it
 *   calls `ipc_port_create`) and AFTER `console_svc_init()` /
 *   `fs_svc_init()` so the well-known port indexes stay deterministic
 *   across boots (console = slot 1, fs read = slot 2, fs write = slot
 *   3, broker = slot 4 in a fresh port table). It must run BEFORE any
 *   module-registry walk that may try to mint a handle for the
 *   broker-svc port. In host tests, the test driver controls ordering
 *   directly.
 *
 * Issue: #302. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md
 * slice 1.
 */

#include "broker_svc.h"

#include "../../tests/harness/svc_subjects.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"

/*
 * Module-private state. A single port handle plus an "initialised"
 * latch. No dynamic allocation; no other state — the recv loop, the
 * op-dispatch step, and the `share_id -> cap_handle_t` side-table
 * land in slice 3/4.
 */
static ipc_port_t g_broker_svc_port = IPC_PORT_INVALID;
static bool g_broker_svc_initialised = false;

broker_svc_result_t broker_svc_init(void) {
  if (g_broker_svc_initialised) {
    return BROKER_SVC_ERR_ALREADY_INIT;
  }

  ipc_port_t handle = IPC_PORT_INVALID;
  ipc_result_t rc = ipc_port_create((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                                    CAP_IPC_SEND,
                                    CAP_IPC_SEND,
                                    &handle);
  if (rc != IPC_OK || handle == IPC_PORT_INVALID) {
    return BROKER_SVC_ERR_PORT_ALLOC;
  }

  g_broker_svc_port = handle;
  g_broker_svc_initialised = true;
  return BROKER_SVC_OK;
}

void broker_svc_reset(void) {
  /* Intentionally do not call ipc_port_destroy() here. Tests that want
   * the port released use ipc_port_table_reset() in the same phase;
   * kernel-side reset is owned by the boot sequence in a future slice
   * if needed. Same convention as console_svc_reset() /
   * fs_svc_reset(). */
  g_broker_svc_port = IPC_PORT_INVALID;
  g_broker_svc_initialised = false;
}

ipc_port_t broker_svc_port(void) {
  return g_broker_svc_port;
}

bool broker_svc_is_initialised(void) {
  return g_broker_svc_initialised;
}
