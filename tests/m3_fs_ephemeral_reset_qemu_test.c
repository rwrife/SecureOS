/**
 * @file m3_fs_ephemeral_reset_qemu_test.c
 * @brief M3-on-M1 substrate peer of `tests/fs_service_ephemeral_reset_test.c`
 *        (slice 4 of plan #277, issue #281).
 *
 * Rides on the real M1 substrate end-to-end (the `_qemu` suffix follows
 * the convention #259/#270/#280 committed to — no QEMU image required,
 * the suffix denotes "rides on the real M1 substrate"):
 *
 *   1. `fs_svc_init()` allocates the two well-known fs ports (slice 1,
 *      #278) owned by `SUBJECT_M3_FS_SVC` and gated by
 *      `CAP_FS_READ` / `CAP_FS_WRITE`.
 *   2. The fs-svc subject is granted CAP_FS_READ + CAP_FS_WRITE on the
 *      legacy bitset and a recv handle is minted for each so the test
 *      driver can drain the staged envelopes via `ipc_recv_h`.
 *   3. The faux-storage shadow is set up via
 *      `launcher_fs_register_app(..., LAUNCHER_FS_MODE_EPHEMERAL)`.
 *      This is what carves the launcher-policy persistence boundary —
 *      ephemeral apps lose their faux data on the launcher-mediated
 *      relaunch hook (`launcher_fs_app_relaunch`) per
 *      `kernel/user/launcher_fs.h:32-45`.
 *   4. `launcher_fs_spawn_app_with_fs_caps(&m, /grant_write=/1, ...)`
 *      (slice 2, #279) produces a live PCB and stamps both fs handles
 *      into `ipc_scratch[8..24)`.
 *   5. `helloapp_entry_fs_demo()` drives both legs; the test driver
 *      drains them and fans the bytes into `launcher_fs_app_{write,
 *      read}` (the same drain pattern as #280).
 *   6. For the `gone_after_relaunch` sub-check we deliberately recycle
 *      the PCB via `process_destroy(app_pid)` + a *fresh*
 *      `launcher_fs_spawn_app_with_fs_caps(...)` — NOT
 *      `launcher_fs_app_relaunch` as the relaunch substitute. The
 *      launcher_fs faux-storage policy is keyed by `cap_subject_id_t`
 *      (per `launcher_fs.h:32-45`), so the launcher-policy hook
 *      (`launcher_fs_app_relaunch`) is what clears ephemeral state at
 *      the registration boundary; the test still invokes it as part of
 *      the substrate-recycle sequence (the plan's risk-section
 *      guidance: "asserted on the registration boundary, not on the
 *      pid"). What `process_destroy` authoritatively buys us is the
 *      handle wipe via `cap_handle_revoke_subject` (#240) — verified
 *      observable here by re-checking the pre-destroy handles after
 *      the recycle.
 *   7. The `no_persist_leak` sub-check spawns a *second*, persistent
 *      peer at the same path *after* the recycle and asserts
 *      `launcher_fs_app_read(...) == LAUNCHER_FS_ERR_NOT_FOUND`,
 *      confirming the ephemeral state did not leak to the persistent
 *      ramfs.
 *
 * Output markers (consumed by build/scripts/test_m3_fs_ephemeral_reset_qemu.sh):
 *   TEST:PASS:m3_fs_ephemeral_reset_qemu:write_to_faux_succeeds
 *   TEST:PASS:m3_fs_ephemeral_reset_qemu:visible_in_same_session
 *   TEST:PASS:m3_fs_ephemeral_reset_qemu:gone_after_relaunch
 *   TEST:PASS:m3_fs_ephemeral_reset_qemu:no_persist_leak
 *   TEST:PASS:m3_fs_ephemeral_reset_qemu
 *
 * Issue: #281. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 4.
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
  printf("TEST:FAIL:m3_fs_ephemeral_reset_qemu:%s\n", reason);
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

/* Mint a recv handle for the fs-svc subject + the legacy-bitset grant
 * that the audit-ring parity hit in ipc_recv_h needs. Mirrors the
 * helper of the same name in tests/m3_fs_persist_allow_qemu_test.c. */
static cap_handle_t fs_svc_setup_recv(capability_id_t cap) {
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M3_FS_SVC, cap) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M3_FS_SVC, cap);
}

/* Drain one envelope on `port` using `recv_h`, then fan the payload
 * into launcher_fs_app_write under the sender subject (which the
 * kernel stamped into rx.sender_subject during ipc_send_h). */
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

/* Drain a read-leg envelope and assert the faux fs returns NOT_FOUND
 * for the same path under the given subject. Used by both the
 * post-recycle ephemeral peer (gone_after_relaunch) and the parallel
 * persistent peer (no_persist_leak). */
static int drain_and_assert_not_found(ipc_port_t port,
                                      cap_handle_t recv_h,
                                      cap_subject_id_t expected_sender,
                                      const char *fail_prefix) {
  ipc_msg_v0 rx = {0};
  ipc_result_t rr = ipc_recv_h(recv_h, port, &rx);
  if (rr != IPC_OK) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s_drain_not_ok", fail_prefix);
    fail(buf);
    return 0;
  }
  if (rx.sender_subject != (uint32_t)expected_sender) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s_sender_mismatch", fail_prefix);
    fail(buf);
    return 0;
  }
  char rb[64] = {0};
  size_t n = 0;
  launcher_fs_result_t pr =
      launcher_fs_app_read(expected_sender, HELLOAPP_FS_DEMO_PATH,
                           rb, sizeof(rb), &n);
  if (pr != LAUNCHER_FS_ERR_NOT_FOUND) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s_expected_not_found", fail_prefix);
    fail(buf);
    return 0;
  }
  return 1;
}

int main(void) {
  reset_world();

  /* (1) fs_svc port allocation (slice 1, #278). */
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

  /* (2) fs-svc recv handles. */
  cap_handle_t fs_recv_write = fs_svc_setup_recv(CAP_FS_WRITE);
  cap_handle_t fs_recv_read  = fs_svc_setup_recv(CAP_FS_READ);
  if (fs_recv_write == CAP_HANDLE_NULL || fs_recv_read == CAP_HANDLE_NULL) {
    fail("fs_svc_recv_setup_failed");
    goto out;
  }

  /* (3) Faux-storage shadow for the spawned app, EPHEMERAL mode. */
  const cap_subject_id_t app = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  if (launcher_fs_register_app(app, LAUNCHER_FS_MODE_EPHEMERAL)
        != LAUNCHER_FS_OK) {
    fail("launcher_fs_register_ephemeral");
    goto out;
  }
  if (launcher_fs_app_mode(app) != LAUNCHER_FS_MODE_EPHEMERAL) {
    fail("launcher_fs_mode_not_ephemeral");
    goto out;
  }
  if (launcher_fs_grant_write(app) != LAUNCHER_FS_OK ||
      launcher_fs_grant_read(app)  != LAUNCHER_FS_OK) {
    fail("launcher_fs_grant_failed");
    goto out;
  }

  /* (4) Spawn the app with both fs handles. */
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

  /* ---- Sub-check 1: write_to_faux_succeeds ---------------------- */
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
  if (!drain_and_persist_write(fs_write_port, fs_recv_write, app)) {
    goto out;
  }
  printf("TEST:PASS:m3_fs_ephemeral_reset_qemu:write_to_faux_succeeds\n");

  /* ---- Sub-check 2: visible_in_same_session --------------------- *
   * The read-leg envelope from the SAME `helloapp_entry_fs_demo`
   * call above is still queued on `fs_read_port`. Drain it and
   * confirm the just-written ephemeral blob is observable through
   * `launcher_fs_app_read` for the same subject in the same session
   * (same PCB, same handles). */
  if (!drain_and_read_back(fs_read_port, fs_recv_read, app)) {
    goto out;
  }
  printf("TEST:PASS:m3_fs_ephemeral_reset_qemu:visible_in_same_session\n");

  /* ---- Sub-check 3: gone_after_relaunch ------------------------- *
   * Real substrate recycle: `process_destroy` cascades
   * `cap_handle_revoke_subject` (#240) so the pre-destroy handles
   * fail `cap_gate_check_handle` post-call; then a fresh
   * `launcher_fs_spawn_app_with_fs_caps` mints NEW handles for the
   * same subject. The launcher_fs faux store is keyed by
   * `cap_subject_id_t` (`launcher_fs.h:32-45`), so the ephemeral
   * data wipe is asserted at the launcher_fs *registration boundary*
   * — invoked via `launcher_fs_app_relaunch` — not at the pid (plan
   * #277 §"Risks/mitigations" risk 3). The substrate proof here is
   * the handle-revocation half; the launcher-policy hook is the
   * data-wipe half. */
  cap_handle_t pre_destroy_read  = sp.read_handle;
  cap_handle_t pre_destroy_write = sp.write_handle;
  if (launcher_fs_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("destroy_failed");
    goto out;
  }
  if (cap_gate_check_handle(pre_destroy_read,  CAP_FS_READ)  != 0 ||
      cap_gate_check_handle(pre_destroy_write, CAP_FS_WRITE) != 0) {
    fail("destroyed_handles_still_live");
    goto out;
  }
  /* Launcher-policy ephemeral wipe hook: drops the launcher_fs
   * grants AND clears faux storage for ephemeral apps. NOT used as
   * a substitute for the process recycle — it sits alongside it. */
  if (launcher_fs_app_relaunch(app) != LAUNCHER_FS_OK) {
    fail("launcher_fs_app_relaunch_failed");
    goto out;
  }
  if (launcher_fs_app_has_read(app) || launcher_fs_app_has_write(app)) {
    fail("grants_should_clear_on_relaunch");
    goto out;
  }
  /* Re-grant launcher_fs read so the post-recycle read can fan out
   * through the faux backend. */
  if (launcher_fs_grant_read(app) != LAUNCHER_FS_OK) {
    fail("regrant_read_after_relaunch");
    goto out;
  }
  /* Fresh spawn (read-only this time — write would deny anyway since
   * we did not re-grant write, and the read path is what proves the
   * faux data was wiped). */
  launcher_fs_spawn_t sp2;
  if (launcher_fs_spawn_app_with_fs_caps(&m, 0, &sp2) != LAUNCHER_OK) {
    fail("respawn_failed");
    goto out;
  }
  if (sp2.read_handle == CAP_HANDLE_NULL) {
    fail("respawn_read_handle_null");
    goto out;
  }
  helloapp_fs_demo_result_t demo2 = {0};
  helloapp_entry_fs_demo(sp2.aspace, fs_read_port, fs_write_port, &demo2);
  /* Write leg must deny — we did NOT re-grant CAP_FS_WRITE. The read
   * leg must reach the port (IPC_OK from ipc_send_h's perspective);
   * the *faux read* itself is what must return NOT_FOUND. */
  if (demo2.write_send_result != IPC_ERR_CAP_DENIED) {
    fail("respawn_write_should_deny_with_no_grant");
    goto out;
  }
  if (demo2.read_send_result != IPC_OK) {
    fail("respawn_read_send_not_ok");
    goto out;
  }
  if (!drain_and_assert_not_found(fs_read_port, fs_recv_read, app,
                                  "gone_after_relaunch_read")) {
    goto out;
  }
  printf("TEST:PASS:m3_fs_ephemeral_reset_qemu:gone_after_relaunch\n");

  /* ---- Sub-check 4: no_persist_leak ----------------------------- *
   * Spawn a SECOND, persistent peer at the same path AFTER the
   * recycle and confirm the faux read returns NOT_FOUND — i.e. the
   * ephemeral blob never spilled upward into the persistent
   * ramfs. The persistent peer uses a distinct subject so the
   * launcher_fs registration boundary is independent. */
  const cap_subject_id_t persistent_peer =
      (cap_subject_id_t)SUBJECT_M2_LAUNCHER; /* low-numbered, distinct. */
  if (launcher_fs_register_app(persistent_peer, LAUNCHER_FS_MODE_PERSISTENT)
        != LAUNCHER_FS_OK) {
    fail("persistent_peer_register");
    goto out;
  }
  if (launcher_fs_grant_read(persistent_peer) != LAUNCHER_FS_OK) {
    fail("persistent_peer_grant_read");
    goto out;
  }
  launcher_manifest_t m_peer = {
      .subject_id       = persistent_peer,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_fs_spawn_t sp_peer;
  if (launcher_fs_spawn_app_with_fs_caps(&m_peer, 0, &sp_peer) != LAUNCHER_OK) {
    fail("persistent_peer_spawn_failed");
    goto out;
  }
  if (sp_peer.read_handle == CAP_HANDLE_NULL) {
    fail("persistent_peer_read_handle_null");
    goto out;
  }
  helloapp_fs_demo_result_t demo_peer = {0};
  helloapp_entry_fs_demo(sp_peer.aspace, fs_read_port, fs_write_port,
                         &demo_peer);
  if (demo_peer.read_send_result != IPC_OK) {
    fail("persistent_peer_read_send_not_ok");
    goto out;
  }
  if (!drain_and_assert_not_found(fs_read_port, fs_recv_read, persistent_peer,
                                  "no_persist_leak_peer_read")) {
    goto out;
  }
  /* Drain the persistent peer's write-leg deny too so the port queue
   * is left clean (no spurious envelope held back); the write leg
   * itself must have denied since we did not grant CAP_FS_WRITE. */
  if (demo_peer.write_send_result != IPC_ERR_CAP_DENIED) {
    fail("persistent_peer_write_should_deny");
    goto out;
  }
  if (launcher_fs_spawn_destroy(sp_peer.pid) != LAUNCHER_OK) {
    fail("persistent_peer_destroy_failed");
    goto out;
  }
  printf("TEST:PASS:m3_fs_ephemeral_reset_qemu:no_persist_leak\n");

  /* Clean up the ephemeral re-spawn. */
  if (launcher_fs_spawn_destroy(sp2.pid) != LAUNCHER_OK) {
    fail("ephemeral_respawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m3_fs_ephemeral_reset_qemu\n");
  return 0;
}
