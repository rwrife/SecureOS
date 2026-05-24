/**
 * @file console_svc.c
 * @brief M2-on-M1 console service implementation — slice 1.
 *
 * See `console_svc.h` for the public contract and
 * `plans/2026-05-23-m2-on-m1-substrate.md` §"Console service module"
 * for the design context.
 *
 * Slice scope (intentionally narrow — issue #268):
 *   - Allocate one well-known port at init.
 *   - Expose the handle for downstream slices (#269, #270, #271).
 *   - Do NOT spin a recv loop or forward bytes to the existing
 *     console driver yet — that lands with the HelloApp slice (#270).
 *
 * Boot-order edge:
 *   `console_svc_init()` MUST run AFTER `ipc_port_table_init()` (it
 *   calls `ipc_port_create`) and BEFORE any module-registry walk that
 *   may try to mint a handle for the console port. In the M1 kernel
 *   boot sequence (`kernel/core/kmain.c`) this slot is between
 *   `ipc_port_table_init` and the module-registry init pass. In host
 *   tests, the test driver controls ordering directly.
 *
 * Issue: #268. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 1.
 */

#include "console_svc.h"

#include "../../tests/harness/m2_subjects.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"

/*
 * Module-private state. A single port handle plus an "initialised"
 * latch. No dynamic allocation; no other state — the recv loop and
 * forwarding bookkeeping land in slice 3.
 */
static ipc_port_t g_console_svc_port = IPC_PORT_INVALID;
static bool g_console_svc_initialised = false;

console_svc_result_t console_svc_init(void) {
  if (g_console_svc_initialised) {
    return CONSOLE_SVC_ERR_ALREADY_INIT;
  }

  ipc_port_t handle = IPC_PORT_INVALID;
  ipc_result_t rc = ipc_port_create((cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC,
                                    CAP_CONSOLE_WRITE,
                                    CAP_CONSOLE_WRITE,
                                    &handle);
  if (rc != IPC_OK || handle == IPC_PORT_INVALID) {
    return CONSOLE_SVC_ERR_PORT_ALLOC;
  }

  g_console_svc_port = handle;
  g_console_svc_initialised = true;
  return CONSOLE_SVC_OK;
}

void console_svc_reset(void) {
  /* Intentionally do not call ipc_port_destroy() here. Tests that want
   * the port released use ipc_port_table_reset() in the same phase;
   * kernel-side reset is owned by the boot sequence in a future
   * slice (M2-SUBSTRATE-005 if needed). */
  g_console_svc_port = IPC_PORT_INVALID;
  g_console_svc_initialised = false;
}

ipc_port_t console_svc_port(void) {
  return g_console_svc_port;
}

bool console_svc_is_initialised(void) {
  return g_console_svc_initialised;
}
