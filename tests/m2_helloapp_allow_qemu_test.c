/**
 * @file m2_helloapp_allow_qemu_test.c
 * @brief Allow-path acceptance for slice 3 of plan #263 / issue #270.
 *
 * Drives the full M2-on-M1 substrate end-to-end on a host build (the
 * `_qemu` tier name follows the BUILD_ROADMAP §5.2 convention that
 * tests in this tier exercise a real `process_create` and a real
 * IPC send instead of poking the gate directly the way the pre-M1
 * `tests/helloapp_allow_test.c` fixture does):
 *
 *   1. Initialises the console service (slice 1 / #268) which
 *      allocates the well-known IPC port owned by
 *      `SUBJECT_M2_CONSOLE_SVC`, gated by `CAP_CONSOLE_WRITE`.
 *   2. Grants the console-service subject `CAP_CONSOLE_WRITE` so
 *      the recv side of the port passes the legacy
 *      `cap_check`-driven audit ring used by `ipc_recv_h`.
 *   3. Spawns HelloApp via `launcher_spawn_app_from_manifest()`
 *      (slice 2 / #269) with a manifest declaring an auto-grant
 *      of `CAP_CONSOLE_WRITE`. The launcher partitions a fresh
 *      `address_space_t`, calls `process_create`, mints the
 *      handle, and writes it little-endian into `ipc_scratch[0..3]`.
 *   4. Invokes `helloapp_run_once(aspace, console_svc_port())` —
 *      the HelloApp module body builds the canonical envelope
 *      and calls `ipc_send_h` exactly once.
 *   5. Asserts:
 *        - `process_is_live_for_tests(pid)` was true between
 *          steps 3 and 4 (real PCB existed).
 *        - The send returned `IPC_OK`.
 *        - The staged envelope drained via `ipc_recv_h` on the
 *          console-svc handle equals `HELLOAPP_BANNER` byte-for-byte
 *          with `sender_subject == SUBJECT_M2_HELLOAPP`.
 *
 * Output markers (consumed by build/scripts/test_m2_helloapp_allow_qemu.sh):
 *   TEST:PASS:helloapp_allowed_console_write
 *   TEST:PASS:helloapp_allow
 *
 * Pure host-side build. The pre-M1 `tests/helloapp_allow_test.c`
 * fixture is intentionally left untouched (plan §"Done when").
 *
 * Issue: #270. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 3.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/svc/console_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "harness/m2_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:helloapp_allow:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  console_svc_reset();
  ipc_port_table_reset();
  launcher_spawn_reset();
}

int main(void) {
  reset_world();

  /* Slice 1: console service port. */
  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("console_svc_init_failed");
    goto out;
  }
  ipc_port_t console_port = console_svc_port();
  if (console_port == IPC_PORT_INVALID) {
    fail("console_port_invalid");
    goto out;
  }

  /* Console-svc subject must hold CAP_CONSOLE_WRITE so the recv-side
   * gate in `ipc_recv_h` passes when the test drains the port. The
   * grant is parallel to the launcher's auto-grant of HelloApp; both
   * subjects need their own grant in the per-subject bitset. */
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC,
                          CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("console_svc_grant_failed");
    goto out;
  }
  cap_handle_t console_recv_handle =
      cap_handle_grant((cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC,
                       CAP_CONSOLE_WRITE);
  if (console_recv_handle == CAP_HANDLE_NULL) {
    fail("console_svc_handle_mint_failed");
    goto out;
  }

  /* Slice 2: spawn HelloApp with an auto-grant of CAP_CONSOLE_WRITE. */
  const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
      .auto_grant_caps  = requested,
      .auto_grant_count = 1u,
  };
  launcher_spawn_t sp;
  if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }
  if (sp.aspace == NULL || sp.aspace->ipc_scratch == NULL) {
    fail("spawned_aspace_or_scratch_null");
    goto out;
  }
  if (sp.granted_handle == CAP_HANDLE_NULL) {
    fail("spawned_handle_null");
    goto out;
  }

  /* Slice 3: run the HelloApp body. */
  ipc_result_t sr = helloapp_run_once(sp.aspace, console_port);
  if (sr != IPC_OK) {
    fail("helloapp_send_not_ok");
    goto out;
  }

  /* The send-side success is the BUILD_ROADMAP §5.2 grant-path
   * acceptance signal; emit it now before draining the envelope so the
   * marker order in the log is unambiguous. */
  printf("TEST:PASS:helloapp_allowed_console_write\n");

  /* Drain the staged envelope through the canonical recv path. The
   * console-svc subject holds CAP_CONSOLE_WRITE + owns the port, so
   * `ipc_recv_h` must return IPC_OK and the payload must match the
   * banner byte-for-byte. */
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(console_recv_handle, console_port, &rx);
  if (rr != IPC_OK) {
    fail("console_drain_not_ok");
    goto out;
  }
  if (rx.sender_subject != (uint32_t)SUBJECT_M2_HELLOAPP) {
    fail("drained_sender_subject_mismatch");
    goto out;
  }
  if (rx.payload_len != (uint32_t)HELLOAPP_BANNER_LEN) {
    fail("drained_payload_len_mismatch");
    goto out;
  }
  if (memcmp(rx.payload, HELLOAPP_BANNER, HELLOAPP_BANNER_LEN) != 0) {
    fail("drained_payload_bytes_mismatch");
    goto out;
  }

  /* Teardown: destroy the spawn (revokes the minted handle as a side
   * effect, per #239 / slice 2's handoff contract). Double-checking
   * the handle dies on revoke is owned by the slice-2 destroy test;
   * here we only confirm the destroy itself returns OK. */
  if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:helloapp_allow\n");
  return 0;
}
