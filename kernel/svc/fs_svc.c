/**
 * @file fs_svc.c
 * @brief M3-on-M1 fs service implementation — slice 1.
 *
 * See `fs_svc.h` for the public contract and
 * `plans/2026-05-24-m3-fs-on-m1-substrate.md` §"Follow-up
 * implementation issues to file" §1 for the design context.
 *
 * Slice scope (intentionally narrow — issue #278):
 *   - Allocate two well-known ports at init (read + write).
 *   - Expose the handles for downstream slices (#279, #280, #281).
 *   - Do NOT spin a recv loop or handle fs_read/fs_write envelopes
 *     yet — that lands with the persist tests in #280.
 *
 * Boot-order edge:
 *   `fs_svc_init()` MUST run AFTER `ipc_port_table_init()` (it calls
 *   `ipc_port_create` twice) and AFTER `console_svc_init()` so the
 *   well-known port indexes stay deterministic across boots (read +
 *   write are slots 2 and 3 in a fresh port table, with console at
 *   slot 1). It must run BEFORE any module-registry walk that mints
 *   handles for the fs ports. In host tests, the test driver controls
 *   ordering directly.
 *
 * Issue: #278. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 1.
 */

#include "fs_svc.h"

#include "../../tests/harness/svc_subjects.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"

/*
 * Module-private state. Two port handles plus an "initialised" latch.
 * No dynamic allocation; no other state — the recv loops and
 * fs-envelope handling land in slice 3 (#280).
 */
static ipc_port_t g_fs_svc_port_read = IPC_PORT_INVALID;
static ipc_port_t g_fs_svc_port_write = IPC_PORT_INVALID;
static bool g_fs_svc_initialised = false;

fs_svc_result_t fs_svc_init(void) {
  if (g_fs_svc_initialised) {
    return FS_SVC_ERR_ALREADY_INIT;
  }

  ipc_port_t read_handle = IPC_PORT_INVALID;
  ipc_result_t rc = ipc_port_create((cap_subject_id_t)SUBJECT_M3_FS_SVC,
                                    CAP_FS_READ,
                                    CAP_FS_READ,
                                    &read_handle);
  if (rc != IPC_OK || read_handle == IPC_PORT_INVALID) {
    return FS_SVC_ERR_PORT_ALLOC_READ;
  }

  ipc_port_t write_handle = IPC_PORT_INVALID;
  rc = ipc_port_create((cap_subject_id_t)SUBJECT_M3_FS_SVC,
                       CAP_FS_WRITE,
                       CAP_FS_WRITE,
                       &write_handle);
  if (rc != IPC_OK || write_handle == IPC_PORT_INVALID) {
    /* Leave the read port allocated; the caller can clean up via
     * ipc_port_table_reset(). Module state stays uninitialised so a
     * retry after reset is well-defined. */
    return FS_SVC_ERR_PORT_ALLOC_WRITE;
  }

  g_fs_svc_port_read = read_handle;
  g_fs_svc_port_write = write_handle;
  g_fs_svc_initialised = true;
  return FS_SVC_OK;
}

void fs_svc_reset(void) {
  /* Intentionally do not call ipc_port_destroy() here. Tests that want
   * the ports released use ipc_port_table_reset() in the same phase;
   * kernel-side reset is owned by the boot sequence in a future slice
   * if needed. Same convention as console_svc_reset(). */
  g_fs_svc_port_read = IPC_PORT_INVALID;
  g_fs_svc_port_write = IPC_PORT_INVALID;
  g_fs_svc_initialised = false;
}

ipc_port_t fs_svc_port_read(void) {
  return g_fs_svc_port_read;
}

ipc_port_t fs_svc_port_write(void) {
  return g_fs_svc_port_write;
}

bool fs_svc_is_initialised(void) {
  return g_fs_svc_initialised;
}
