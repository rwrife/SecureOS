/**
 * @file m2_launcher_console_qemu_test.c
 * @brief `_qemu`-tier launcher_console peer covering all four §5.2
 *        launcher-mediation markers via the real M1 substrate.
 *
 * Slice 4 of `plans/2026-05-23-m2-on-m1-substrate.md` (issue #271).
 *
 * The pre-M1 fast fixture `tests/launcher_console_test.c` exercises the
 * same four markers entirely through the in-process `launcher_app_*`
 * shim — no PCB, no IPC, no handles. This `_qemu` peer is the
 * substrate-tier counterpart: every assertion is driven by spawning
 * HelloApp through `launcher_spawn_app_from_manifest()`, sending /
 * denying through `ipc_send_h()` keyed off the minted handle, and
 * (for the revoke marker) tearing the spawn down with
 * `process_destroy()` whose `cap_handle_revoke_subject` cascade is
 * already wired at `kernel/proc/process.c:174` (pre-audit retired the
 * conditional slice 5).
 *
 * Four asserted markers (BUILD_ROADMAP §5.2 / plan §5.2):
 *
 *   TEST:PASS:launcher_console_deny_without_grant
 *     Spawn HelloApp with an empty auto-grant manifest. The launcher
 *     leaves `ipc_scratch[0..3]` zeroed, HelloApp decodes
 *     `CAP_HANDLE_NULL`, `ipc_send_h` takes the canonical handle-deny
 *     path and the IPC layer emits exactly one
 *     `CAP:DENY:<subj>:console_write:-` marker validated through
 *     `cap_deny_marker_validate()`.
 *
 *   TEST:PASS:launcher_console_allow_after_grant
 *     Spawn HelloApp with `CAP_CONSOLE_WRITE` auto-granted. The
 *     allow-tier `ipc_send_h` returns `IPC_OK` and the staged envelope
 *     drains via `ipc_recv_h` byte-for-byte equal to `HELLOAPP_BANNER`
 *     with `sender_subject == SUBJECT_M2_HELLOAPP`.
 *
 *   TEST:PASS:launcher_console_regression_bypass_denied
 *     Direct cap-gate call by the spawned PCB's subject *without* the
 *     launcher path must fail closed — `cap_console_write_gate` on a
 *     no-grant subject returns `CAP_ERR_MISSING`. This is the same
 *     bypass-prevention assertion the pre-M1 fixture makes, but the
 *     subject under test is now a real substrate-spawned PCB.
 *
 *   TEST:PASS:launcher_console_revoke_restores_deny
 *     Spawn HelloApp with the grant, prove the send works, then call
 *     `launcher_spawn_destroy(pid)` (which fans out to
 *     `process_destroy` → `cap_handle_revoke_subject`). A subsequent
 *     `ipc_send_h` keyed off the now-stale handle must return
 *     `IPC_ERR_CAP_DENIED` and emit a canonical deny marker.
 *
 * Deny vocabulary is never formatted locally — every deny line is
 * validated through `cap_deny_marker_validate()` against the §4
 * grammar (`CAP:DENY:<actor>:<cap>:<resource>\n`) and is checked to
 * carry the `console_write` cap name. This keeps the test honest
 * about the single source of truth in `kernel/cap/cap_deny_marker.{c,h}`
 * (#221 / PR #244).
 *
 * Launched by `build/scripts/test_m2_launcher_console_qemu.sh`
 * (dispatched via `build/scripts/test.sh m2_launcher_console_qemu`).
 *
 * The pre-M1 fixture `tests/launcher_console_test.c` is intentionally
 * left untouched (plan §Acceptance demo).
 *
 * Issue: #271. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 4.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_gate.h"
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
static char g_capture[4096];

static void fail(const char *reason) {
  printf("TEST:FAIL:m2_launcher_console_qemu:%s\n", reason);
  g_fail = 1;
}

/*
 * stdout capture shim — same shape as the one in
 * `tests/m2_helloapp_deny_qemu_test.c`. Duplicated rather than
 * factored out per the substrate plan's "helpers stay near the test
 * that needs them" convention (slice budget is ~120 LoC per helper).
 */
typedef ipc_result_t (*capture_fn)(void);
static ipc_result_t g_capture_result;

static long capture_stdout(capture_fn fn) {
  fflush(stdout);
  FILE *tmp = tmpfile();
  if (!tmp) return -1;
  int saved_fd = dup(fileno(stdout));
  if (saved_fd < 0) { fclose(tmp); return -1; }
  if (dup2(fileno(tmp), fileno(stdout)) < 0) {
    close(saved_fd); fclose(tmp); return -1;
  }
  g_capture_result = fn();
  fflush(stdout);
  dup2(saved_fd, fileno(stdout));
  close(saved_fd);
  fseek(tmp, 0L, SEEK_SET);
  size_t n = fread(g_capture, 1u, sizeof(g_capture) - 1u, tmp);
  g_capture[n] = '\0';
  fclose(tmp);
  return (long)n;
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

/* Shared init: bring up the console service port and grant the recv
 * side `CAP_CONSOLE_WRITE` so drains pass `ipc_recv_h`. Returns the
 * port handle, or `IPC_PORT_INVALID` on failure. Also mints a recv
 * handle for the console-svc subject and stores it in `*out_recv`. */
static ipc_port_t bring_up_console_svc(cap_handle_t *out_recv) {
  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("console_svc_init_failed");
    return IPC_PORT_INVALID;
  }
  ipc_port_t port = console_svc_port();
  if (port == IPC_PORT_INVALID) {
    fail("console_port_invalid");
    return IPC_PORT_INVALID;
  }
  if (cap_grant_for_tests((cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC,
                          CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("console_svc_grant_failed");
    return IPC_PORT_INVALID;
  }
  cap_handle_t h = cap_handle_grant((cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC,
                                    CAP_CONSOLE_WRITE);
  if (h == CAP_HANDLE_NULL) {
    fail("console_svc_handle_mint_failed");
    return IPC_PORT_INVALID;
  }
  *out_recv = h;
  return port;
}

/* Captured-call context (one set of globals reused across the four
 * marker phases — each phase resets it before its capture). */
static const address_space_t *g_run_aspace;
static ipc_port_t              g_run_port;
static cap_handle_t            g_run_handle;
static ipc_msg_v0              g_run_msg;

static ipc_result_t do_helloapp_run(void) {
  return helloapp_run_once(g_run_aspace, g_run_port);
}

static ipc_result_t do_send_via_handle(void) {
  return ipc_send_h(g_run_handle, g_run_port, &g_run_msg);
}

/* Validate the captured stdout buffer holds exactly one canonical
 * CAP:DENY marker line that names the `console_write` capability. */
static int assert_console_write_deny_marker(const char *phase) {
  char reason[64] = {0};
  int vrc = cap_deny_marker_validate(g_capture, reason, sizeof(reason));
  if (vrc != 0) {
    printf("TEST:FAIL:m2_launcher_console_qemu:%s:deny_marker_invalid:%s\n",
           phase, reason);
    printf("TEST:FAIL:m2_launcher_console_qemu:%s:captured=%s\n",
           phase, g_capture);
    g_fail = 1;
    return -1;
  }
  if (strstr(g_capture, ":console_write:") == NULL) {
    printf("TEST:FAIL:m2_launcher_console_qemu:%s:wrong_cap_in_marker:captured=%s\n",
           phase, g_capture);
    g_fail = 1;
    return -1;
  }
  return 0;
}

int main(void) {
  printf("TEST:START:m2_launcher_console_qemu\n");
  reset_world();

  cap_handle_t console_recv = CAP_HANDLE_NULL;
  ipc_port_t console_port = bring_up_console_svc(&console_recv);
  if (console_port == IPC_PORT_INVALID) goto out;

  /* --------------------------------------------------------------
   * Phase 1: deny_without_grant
   * Spawn HelloApp with an empty auto-grant manifest. The launcher
   * leaves the scratch slot zeroed -> HelloApp decodes
   * CAP_HANDLE_NULL -> ipc_send_h takes the handle-deny path.
   * -------------------------------------------------------------- */
  {
    launcher_manifest_t m = {
        .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
        .auto_grant_caps  = NULL,
        .auto_grant_count = 0u,
    };
    launcher_spawn_t sp;
    if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
      fail("phase1_launcher_spawn_failed");
      goto out;
    }
    if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
      fail("phase1_spawned_pcb_not_live");
      goto out;
    }
    if (sp.granted_handle != CAP_HANDLE_NULL) {
      fail("phase1_no_grant_manifest_still_minted_handle");
      goto out;
    }

    g_run_aspace = sp.aspace;
    g_run_port   = console_port;
    if (capture_stdout(do_helloapp_run) < 0) {
      fail("phase1_capture_stdout_failed");
      goto out;
    }
    if (g_capture_result != IPC_ERR_CAP_DENIED) {
      fail("phase1_helloapp_run_did_not_deny");
      goto out;
    }
    if (assert_console_write_deny_marker("phase1") != 0) goto out;

    /* Deny path MUST NOT stage. */
    ipc_msg_v0 rx = {0};
    if (ipc_port_consume(console_port, &rx) == IPC_OK) {
      fail("phase1_deny_path_staged_envelope");
      goto out;
    }

    if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
      fail("phase1_launcher_spawn_destroy_failed");
      goto out;
    }
    printf("TEST:PASS:launcher_console_deny_without_grant\n");
  }

  /* --------------------------------------------------------------
   * Phase 2: allow_after_grant
   * Spawn HelloApp with CAP_CONSOLE_WRITE auto-granted; expect the
   * send to succeed and the drained envelope to match the banner.
   * -------------------------------------------------------------- */
  process_id_t allow_pid = PID_INVALID;
  cap_handle_t allow_handle = CAP_HANDLE_NULL;
  address_space_t *allow_aspace = NULL;
  {
    const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
    launcher_manifest_t m = {
        .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
        .auto_grant_caps  = requested,
        .auto_grant_count = 1u,
    };
    launcher_spawn_t sp;
    if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
      fail("phase2_launcher_spawn_failed");
      goto out;
    }
    if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
      fail("phase2_spawned_pcb_not_live");
      goto out;
    }
    if (sp.granted_handle == CAP_HANDLE_NULL) {
      fail("phase2_spawned_handle_null");
      goto out;
    }
    allow_pid    = sp.pid;
    allow_handle = sp.granted_handle;
    allow_aspace = sp.aspace;

    ipc_result_t sr = helloapp_run_once(sp.aspace, console_port);
    if (sr != IPC_OK) {
      fail("phase2_helloapp_send_not_ok");
      goto out;
    }

    ipc_msg_v0 rx = {0};
    if (ipc_recv_h(console_recv, console_port, &rx) != IPC_OK) {
      fail("phase2_console_drain_not_ok");
      goto out;
    }
    if (rx.sender_subject != (uint32_t)SUBJECT_M2_HELLOAPP) {
      fail("phase2_drained_sender_subject_mismatch");
      goto out;
    }
    if (rx.payload_len != (uint32_t)HELLOAPP_BANNER_LEN ||
        memcmp(rx.payload, HELLOAPP_BANNER, HELLOAPP_BANNER_LEN) != 0) {
      fail("phase2_drained_payload_mismatch");
      goto out;
    }
    printf("TEST:PASS:launcher_console_allow_after_grant\n");
  }

  /* --------------------------------------------------------------
   * Phase 3: regression_bypass_denied
   * The substrate-spawned HelloApp subject must NOT be able to call
   * the cap gate directly. The launcher-handed handle is the only
   * sanctioned path. We assert the direct gate call fails closed on
   * a fresh spawn whose manifest carries no auto-grant — i.e. the
   * per-subject bitset is still empty.
   * -------------------------------------------------------------- */
  {
    /* Reset and re-bring-up so the bitset is clean for this phase
     * (phase 2 granted CAP_CONSOLE_WRITE to SUBJECT_M2_HELLOAPP
     * through the launcher's cap_table_grant; we want to test the
     * deny-by-default direct-gate path, not phase 2's leftover). */
    reset_world();
    console_recv = CAP_HANDLE_NULL;
    console_port = bring_up_console_svc(&console_recv);
    if (console_port == IPC_PORT_INVALID) goto out;

    launcher_manifest_t m = {
        .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
        .auto_grant_caps  = NULL,
        .auto_grant_count = 0u,
    };
    launcher_spawn_t sp;
    if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
      fail("phase3_launcher_spawn_failed");
      goto out;
    }
    size_t bytes_written = 0u;
    cap_result_t cr = cap_console_write_gate(
        (cap_subject_id_t)SUBJECT_M2_HELLOAPP, "bypass", &bytes_written);
    if (cr != CAP_ERR_MISSING) {
      fail("phase3_direct_gate_not_denied");
      goto out;
    }
    if (bytes_written != 0u) {
      fail("phase3_direct_gate_wrote_bytes");
      goto out;
    }
    if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
      fail("phase3_launcher_spawn_destroy_failed");
      goto out;
    }
    printf("TEST:PASS:launcher_console_regression_bypass_denied\n");
  }

  /* --------------------------------------------------------------
   * Phase 4: revoke_restores_deny
   * Spawn HelloApp with the grant, prove the send works, capture
   * the handle, then destroy the PCB. The cap_handle_revoke_subject
   * cascade (kernel/proc/process.c:174) must invalidate the handle,
   * so a subsequent ipc_send_h keyed off the same handle returns
   * IPC_ERR_CAP_DENIED with a canonical marker.
   * -------------------------------------------------------------- */
  {
    reset_world();
    console_recv = CAP_HANDLE_NULL;
    console_port = bring_up_console_svc(&console_recv);
    if (console_port == IPC_PORT_INVALID) goto out;

    const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
    launcher_manifest_t m = {
        .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
        .auto_grant_caps  = requested,
        .auto_grant_count = 1u,
    };
    launcher_spawn_t sp;
    if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
      fail("phase4_launcher_spawn_failed");
      goto out;
    }
    allow_pid    = sp.pid;
    allow_handle = sp.granted_handle;
    allow_aspace = sp.aspace;
    if (allow_handle == CAP_HANDLE_NULL) {
      fail("phase4_handle_null");
      goto out;
    }

    /* Allowed send first — proves the handle is live pre-revoke. */
    if (helloapp_run_once(allow_aspace, console_port) != IPC_OK) {
      fail("phase4_pre_revoke_send_failed");
      goto out;
    }
    /* Drain so the next send has a free slot. */
    ipc_msg_v0 rx = {0};
    if (ipc_recv_h(console_recv, console_port, &rx) != IPC_OK) {
      fail("phase4_pre_revoke_drain_failed");
      goto out;
    }

    /* Tear the spawn down — this fans out to
     * process_destroy -> cap_handle_revoke_subject. */
    if (launcher_spawn_destroy(allow_pid) != LAUNCHER_OK) {
      fail("phase4_launcher_spawn_destroy_failed");
      goto out;
    }

    /* Build a canonical envelope and try to send via the stale
     * handle. We do not call helloapp_run_once here because the
     * aspace was freed by the destroy; we drive ipc_send_h
     * directly with a stack-resident envelope. */
    memset(&g_run_msg, 0, sizeof(g_run_msg));
    g_run_msg.abi_version    = (uint16_t)OS_ABI_VERSION;
    g_run_msg.flags          = 0u;
    g_run_msg.payload_len    = (uint32_t)HELLOAPP_BANNER_LEN;
    g_run_msg.sender_subject = (uint32_t)SUBJECT_M2_HELLOAPP;
    memcpy(g_run_msg.payload, HELLOAPP_BANNER, HELLOAPP_BANNER_LEN);
    g_run_handle = allow_handle;
    g_run_port   = console_port;

    if (capture_stdout(do_send_via_handle) < 0) {
      fail("phase4_capture_stdout_failed");
      goto out;
    }
    if (g_capture_result != IPC_ERR_CAP_DENIED) {
      fail("phase4_post_revoke_send_did_not_deny");
      goto out;
    }
    if (assert_console_write_deny_marker("phase4") != 0) goto out;

    /* Deny path MUST NOT stage. */
    ipc_msg_v0 rx2 = {0};
    if (ipc_port_consume(console_port, &rx2) == IPC_OK) {
      fail("phase4_deny_path_staged_envelope");
      goto out;
    }
    printf("TEST:PASS:launcher_console_revoke_restores_deny\n");
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m2_launcher_console_qemu\n");
  return 0;
}
