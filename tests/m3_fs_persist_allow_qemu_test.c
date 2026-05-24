/**
 * @file m3_fs_persist_allow_qemu_test.c
 * @brief M3-on-M1 substrate peer of `tests/fs_service_persist_allow_test.c`
 *        (slice 3 of plan #277, issue #280).
 *
 * Rides on the real M1 substrate (no QEMU image — the `_qemu` suffix
 * follows the same convention #259/#270 committed to):
 *
 *   1. `fs_svc_init()` allocates the two well-known fs ports (slice 1,
 *      #278) owned by `SUBJECT_M3_FS_SVC` and gated by
 *      `CAP_FS_READ` / `CAP_FS_WRITE`.
 *   2. The fs-svc subject is granted CAP_FS_READ + CAP_FS_WRITE on the
 *      legacy bitset and a recv handle is minted for each, so the test
 *      driver can drain the ports via `ipc_recv_h` standing in for the
 *      future fs_svc recv loop.
 *   3. The faux-storage shadow is set up for the app subject via
 *      `launcher_fs_register_app(..., LAUNCHER_FS_MODE_PERSISTENT)` and
 *      explicit `launcher_fs_grant_*` calls, mirroring how a real
 *      fs_svc loop would fan out to `launcher_fs_app_*` per the plan's
 *      §"What changes #2".
 *   4. `launcher_fs_spawn_app_with_fs_caps(..., grant_write=true, ...)`
 *      (slice 2, #279) produces a live PCB with both fs handles
 *      stamped LE64 into `ipc_scratch[8..16)` / `[16..24)`.
 *   5. `helloapp_entry_fs_demo()` (this slice) reads the handles,
 *      issues `ipc_send_h(write_handle, fs_write_port, ...)` followed
 *      by `ipc_send_h(read_handle, fs_read_port, ...)`.
 *   6. The test driver drains the staged envelopes and forwards them
 *      to `launcher_fs_app_write` / `launcher_fs_app_read` (the fan-out
 *      a real fs_svc loop would perform). Persistence across a real
 *      `process_destroy` + re-`launcher_fs_spawn_app_with_fs_caps`
 *      cycle is the relaunch-round-trip sub-check.
 *
 * The five `_qemu` sub-check markers preserve the spelling of the
 * existing host-fixture sub-checks (`cap_present`, `write_succeeds`,
 * `read_back_after_close`, `relaunch_round_trip`) with a `_qemu`
 * suffix, plus the umbrella `m3_fs_persist_allow_qemu`. They are
 * consumed by `build/scripts/test_m3_fs_persist_allow_qemu.sh`.
 *
 * Issue: #280. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 3.
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
#include "../kernel/svc/fs_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "../kernel/user/launcher_fs.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:m3_fs_persist_allow_qemu:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  fs_svc_reset();
  ipc_port_table_reset();
  launcher_fs_reset();
  launcher_spawn_reset();
}

/* Mint a recv handle for the fs-svc subject and the legacy bitset
 * grant the audit-ring parity hit in ipc_recv_h needs. Returns
 * CAP_HANDLE_NULL on failure (treat as a setup error). */
static cap_handle_t fs_svc_setup_recv(capability_id_t cap) {
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M3_FS_SVC, cap) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M3_FS_SVC, cap);
}

/* Drain one envelope on `port` using `recv_h`, forward the payload to
 * launcher_fs_app_write under the app's subject (which the kernel
 * stamped into rx.sender_subject during ipc_send_h). The faux-fs key
 * is pinned by helloapp's HELLOAPP_FS_DEMO_PATH; the value is the
 * payload bytes (NUL-terminated copy). */
static int drain_and_persist_write(ipc_port_t port,
                                   cap_handle_t recv_h,
                                   cap_subject_id_t expected_sender) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, port, &rx);
  if (rr != IPC_OK) {
    fail("write_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_sender) {
    fail("write_drain_sender_mismatch");
    return 0;
  }
  if (rx.payload_len != (uint32_t)HELLOAPP_FS_DEMO_BLOB_LEN ||
      memcmp(rx.payload, HELLOAPP_FS_DEMO_BLOB,
             HELLOAPP_FS_DEMO_BLOB_LEN) != 0) {
    fail("write_drain_payload_mismatch");
    return 0;
  }
  /* Copy to a NUL-terminated buffer so launcher_fs_app_write (a C-string
   * API) consumes the same bytes the IPC payload carried. */
  char content[64] = {0};
  memcpy(content, rx.payload, HELLOAPP_FS_DEMO_BLOB_LEN);
  launcher_fs_result_t pr =
      launcher_fs_app_write(expected_sender,
                            HELLOAPP_FS_DEMO_PATH,
                            content);
  if (pr != LAUNCHER_FS_OK) {
    fail("launcher_fs_app_write_not_ok");
    return 0;
  }
  return 1;
}

/* Drain the read-leg envelope, then read back from the faux fs under
 * the sender subject and confirm the bytes are intact. */
static int drain_and_read_back(ipc_port_t port,
                               cap_handle_t recv_h,
                               cap_subject_id_t expected_sender) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, port, &rx);
  if (rr != IPC_OK) {
    fail("read_drain_not_ok");
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_sender) {
    fail("read_drain_sender_mismatch");
    return 0;
  }
  if (rx.payload_len != (uint32_t)HELLOAPP_FS_DEMO_PATH_LEN ||
      memcmp(rx.payload, HELLOAPP_FS_DEMO_PATH,
             HELLOAPP_FS_DEMO_PATH_LEN) != 0) {
    fail("read_drain_payload_mismatch");
    return 0;
  }
  char buf[64] = {0};
  size_t n = 0;
  launcher_fs_result_t pr =
      launcher_fs_app_read(expected_sender, HELLOAPP_FS_DEMO_PATH,
                           buf, sizeof(buf), &n);
  if (pr != LAUNCHER_FS_OK) {
    fail("launcher_fs_app_read_not_ok");
    return 0;
  }
  if (n != HELLOAPP_FS_DEMO_BLOB_LEN ||
      memcmp(buf, HELLOAPP_FS_DEMO_BLOB,
             HELLOAPP_FS_DEMO_BLOB_LEN) != 0) {
    fail("read_back_content_mismatch");
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  /* fs_svc port allocation (slice 1). */
  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    goto out;
  }
  ipc_port_t fs_read_port  = fs_svc_port_read();
  ipc_port_t fs_write_port = fs_svc_port_write();
  if (fs_read_port == IPC_PORT_INVALID || fs_write_port == IPC_PORT_INVALID) {
    fail("fs_ports_invalid");
    goto out;
  }

  /* fs-svc recv handles. */
  cap_handle_t fs_recv_write = fs_svc_setup_recv(CAP_FS_WRITE);
  cap_handle_t fs_recv_read  = fs_svc_setup_recv(CAP_FS_READ);
  if (fs_recv_write == CAP_HANDLE_NULL || fs_recv_read == CAP_HANDLE_NULL) {
    fail("fs_svc_recv_setup_failed");
    goto out;
  }

  /* Faux-storage shadow for the spawned app, persistent mode. The
   * write/read grants here are what `launcher_fs_app_write/_read` gate
   * on inside the fan-out; the IPC-side gate is enforced separately by
   * the cap_handle path in ipc_send_h. */
  const cap_subject_id_t app = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  if (launcher_fs_register_app(app, LAUNCHER_FS_MODE_PERSISTENT)
        != LAUNCHER_FS_OK) {
    fail("launcher_fs_register_persistent");
    goto out;
  }
  if (launcher_fs_app_mode(app) != LAUNCHER_FS_MODE_PERSISTENT) {
    fail("launcher_fs_mode_not_persistent");
    goto out;
  }
  if (launcher_fs_grant_write(app) != LAUNCHER_FS_OK ||
      launcher_fs_grant_read(app)  != LAUNCHER_FS_OK) {
    fail("launcher_fs_grant_failed");
    goto out;
  }

  /* Slice 2: spawn the app with both fs handles. */
  launcher_manifest_t m = {
      .subject_id       = app,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_fs_spawn_t sp;
  if (launcher_fs_spawn_app_with_fs_caps(&m, 1, &sp) != LAUNCHER_OK) {
    fail("launcher_fs_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }
  if (sp.read_handle == CAP_HANDLE_NULL ||
      sp.write_handle == CAP_HANDLE_NULL) {
    fail("spawned_fs_handles_null");
    goto out;
  }
  if (cap_gate_check_handle(sp.read_handle,  CAP_FS_READ)  != 1 ||
      cap_gate_check_handle(sp.write_handle, CAP_FS_WRITE) != 1) {
    fail("spawned_fs_handles_gate_check");
    goto out;
  }

  /* Sub-check 1 (cap_present_qemu): persistent registration + both
   * fs handles minted and gate-checked. */
  printf("TEST:PASS:m3_fs_persist_allow_qemu:cap_present_qemu\n");

  /* Slice 3: helloapp fs-demo. Drives both legs of the round-trip. */
  helloapp_fs_demo_result_t demo = {0};
  helloapp_entry_fs_demo(sp.aspace, fs_read_port, fs_write_port, &demo);
  if (demo.write_send_result != IPC_OK) {
    fail("helloapp_fs_demo_write_not_ok");
    goto out;
  }
  if (demo.read_send_result != IPC_OK) {
    fail("helloapp_fs_demo_read_not_ok");
    goto out;
  }

  /* Drain the write envelope on fs_write_port; fan out to launcher_fs. */
  if (!drain_and_persist_write(fs_write_port, fs_recv_write, app)) {
    goto out;
  }
  printf("TEST:PASS:m3_fs_persist_allow_qemu:write_succeeds_qemu\n");

  /* Drain the read-request envelope on fs_read_port; serve it from
   * the faux fs and confirm contents round-trip. */
  if (!drain_and_read_back(fs_read_port, fs_recv_read, app)) {
    goto out;
  }
  printf("TEST:PASS:m3_fs_persist_allow_qemu:read_back_after_close_qemu\n");

  /* Sub-check 4 (relaunch_round_trip_qemu): destroy the PCB (which
   * revokes the minted fs handles via #239's cascade), re-spawn with
   * fresh handles, and confirm the persistent blob is still readable.
   * The launcher_fs grants do NOT survive `_relaunch`, mirroring the
   * existing host fixture's invariant from PR #88. */
  if (launcher_fs_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("first_destroy_failed");
    goto out;
  }
  if (cap_gate_check_handle(sp.write_handle, CAP_FS_WRITE) != 0 ||
      cap_gate_check_handle(sp.read_handle,  CAP_FS_READ)  != 0) {
    fail("destroyed_handles_still_live");
    goto out;
  }
  if (launcher_fs_app_relaunch(app) != LAUNCHER_FS_OK) {
    fail("launcher_fs_app_relaunch_failed");
    goto out;
  }
  if (launcher_fs_app_has_read(app) || launcher_fs_app_has_write(app)) {
    fail("grants_should_clear_on_relaunch");
    goto out;
  }
  /* Re-grant launcher_fs read so the post-relaunch read can fan out. */
  if (launcher_fs_grant_read(app) != LAUNCHER_FS_OK) {
    fail("regrant_read_after_relaunch");
    goto out;
  }

  launcher_fs_spawn_t sp2;
  if (launcher_fs_spawn_app_with_fs_caps(&m, 0, &sp2) != LAUNCHER_OK) {
    fail("respawn_failed");
    goto out;
  }
  helloapp_fs_demo_result_t demo2 = {0};
  helloapp_entry_fs_demo(sp2.aspace, fs_read_port, fs_write_port, &demo2);
  /* No write handle this round, so the write leg must DENY (this also
   * reaffirms the deny path is wired even on the relaunch round-trip).
   * The read leg must still succeed. */
  if (demo2.write_send_result != IPC_ERR_CAP_DENIED) {
    fail("respawn_write_should_deny_with_no_grant");
    goto out;
  }
  if (demo2.read_send_result != IPC_OK) {
    fail("respawn_read_not_ok");
    goto out;
  }
  if (!drain_and_read_back(fs_read_port, fs_recv_read, app)) {
    goto out;
  }
  if (launcher_fs_spawn_destroy(sp2.pid) != LAUNCHER_OK) {
    fail("second_destroy_failed");
    goto out;
  }
  printf("TEST:PASS:m3_fs_persist_allow_qemu:relaunch_round_trip_qemu\n");

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m3_fs_persist_allow_qemu\n");
  return 0;
}
