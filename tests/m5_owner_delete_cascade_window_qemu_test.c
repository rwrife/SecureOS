/**
 * @file m5_owner_delete_cascade_window_qemu_test.c
 * @brief M5-on-M1 substrate peer — `BROKER_OP_DELETE_OWNER` window /
 *        session-leg cascade (slice 005c of plan
 *        plans/2026-05-26-m5-wm-cascade-on-substrate.md, issue #387).
 *
 * Companion to the slice 003/004 allow/deny peers
 * (`m5_owner_delete_cascade_{allow,deny}_qemu_test.c`). Where those
 * peers focus on the cap-handle leg of the cascade (subtree revoke +
 * cap.cascade audit events for the delegated handle), this peer
 * focuses on the **second class of owned resource** the M5 plan
 * surfaces: per-owner window-manager sessions. The plan §"Design
 * surface" wires this as step 3.5 of `broker_svc_delete_owner`,
 * between `cap_handle_revoke_subtree` (step 3) and `process_destroy`
 * (step 4). Step 3.5 was implemented in PR #363 (issue #350 slice
 * 005b); this test is the substrate-end-to-end peer that asserts
 * the WM session-side cascade outputs on top of the merged
 * orchestrator.
 *
 * Topology under test (same shape as the merged
 * m5_owner_delete_cascade_allow_qemu peer, with the session leg
 * pre-populated via the shared `session_manager_stub` harness):
 *
 *   1. `broker_svc_init()` + `fs_svc_init()` bring up the broker
 *      port and a real CAP_FS_READ-gated fs port to point a
 *      delegated handle at — needed for the
 *      `delegated_gfx_caps_invalid` sub-check below.
 *   2. `launcher_broker_spawn_app_with_broker_cap()` produces a
 *      live owner PCB (subject = SUBJECT_M2_HELLOAPP) with the
 *      broker send handle stamped LE64 into `ipc_scratch[24..32)`.
 *   3. **Two** sessions owned by the owner subject are injected via
 *      `sm_stub_inject` — the cascade must drain both. A third
 *      session owned by a different bystander subject is injected
 *      to prove the enumerator predicate is correctly subject-
 *      scoped and does NOT drain unrelated sessions.
 *   4. The owner delegates a CAP_GFX_FRAMEBUFFER handle to a
 *      distinct recipient subject via `cap_handle_grant_child`
 *      parented on the owner's broker-port handle so the §5.5
 *      "delegated gfx caps invalid" bullet has a concrete
 *      handle to assert against post-cascade.
 *   5. `broker_svc_delete_owner(owner, owner, owner_broker_h,
 *      PID_INVALID, &n)` runs the six-step cascade with the PCB
 *      teardown intentionally suppressed (`PID_INVALID`) so the
 *      session-leg sub-checks below pin the work done by step 3.5
 *      specifically — not the belt-and-suspenders
 *      `cap_handle_revoke_subject` inside `process_destroy`.
 *
 * Sub-markers emitted (consumed by
 * build/scripts/test_m5_owner_delete_cascade_window_qemu.sh):
 *
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:owned_sessions_destroyed
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:bystander_session_preserved
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:delegated_gfx_caps_invalid
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:session_slot_recyclable
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:double_delete_idempotent_session_leg
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_recorded
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu:audit_wm_cascade_done_recorded
 *   TEST:PASS:m5_owner_delete_cascade_window_qemu
 *
 * Audit markers PASS (not SKIP): step 3.5 in `kernel/svc/broker_svc.c`
 * already emits `CAP_AUDIT_OP_CASCADE_REVOKE` per drained session and
 * the terminal `CAP_AUDIT_OP_CASCADE_DONE` rolls the session-leg
 * count into its `capability_id` payload. The cap-side equivalents
 * flipped to PASS via #370 / PR #374, and the WM cascade events ride
 * on the same audit-ring path — so the plan's "investigate first"
 * note resolves to PASS here.
 *
 * Issue: #387. Plan: plans/2026-05-26-m5-wm-cascade-on-substrate.md
 * slice 005c.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/proc/proc_sched.h"
#include "../kernel/svc/broker_svc.h"
#include "../kernel/svc/fs_svc.h"
#include "../kernel/user/launcher.h"
#include "harness/session_manager_stub.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:m5_owner_delete_cascade_window_qemu:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  proc_sched_reset();
  broker_svc_reset();
  fs_svc_reset();
  cap_broker_reset();
  ipc_port_table_reset();
  launcher_spawn_reset();
  sm_stub_reset();
}

int main(void) {
  reset_world();

  /* Bring up broker_svc + fs_svc (the latter so cap_handle_grant_child
   * has a real cap target for the delegated_gfx_caps_invalid handle). */
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init_failed");
    goto out;
  }
  if (broker_svc_port() == IPC_PORT_INVALID) {
    fail("broker_port_invalid");
    goto out;
  }
  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    goto out;
  }

  /* Spawn the owner PCB with a real broker send handle stamped into
   * ipc_scratch[24..32) — same shape as the cap-leg peer
   * (m5_owner_delete_cascade_allow_qemu_test.c). */
  const cap_subject_id_t owner     = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  /* `cap_table_grant` validates subject_id < CAP_TABLE_MAX_SUBJECTS (8u),
   * so the delegated-handle recipient must be in [0, 8). Subjects
   * 1..5 are the M2/M3/M4 substrate services + launcher; SUBJECT_M5_ADMIN
   * (6) is reserved for the cascade authority predicate; that leaves 7
   * for the recipient. The session-only bystander subject is unconstrained
   * by the cap-table (the sm_stub is a pure in-memory table) so we reuse
   * SUBJECT_M5_ADMIN (6) for it — orthogonal from the cap leg. */
  const cap_subject_id_t bystander = (cap_subject_id_t)SUBJECT_M5_ADMIN;
  const cap_subject_id_t recipient = (cap_subject_id_t)7u;
  launcher_manifest_t m = {
      .subject_id       = owner,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_broker_spawn_t sp;
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_broker_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID ||
      sp.broker_handle == CAP_HANDLE_NULL) {
    fail("spawned_pcb_bad");
    goto out;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("spawned_broker_handle_pre_gate");
    goto out;
  }

  /* Inject two sessions owned by the owner subject + one owned by an
   * unrelated bystander subject. The bystander row guards against a
   * regression where the cascade walker drains the whole table
   * rather than filtering on subject. */
  unsigned int sid_a = 0u;
  unsigned int sid_b = 0u;
  unsigned int sid_x = 0u;
  if (sm_stub_inject(owner, &sid_a) != 0 ||
      sm_stub_inject(owner, &sid_b) != 0 ||
      sm_stub_inject(bystander, &sid_x) != 0) {
    fail("session_inject_failed");
    goto out;
  }
  if (!sm_stub_in_use(sid_a) || !sm_stub_in_use(sid_b) ||
      !sm_stub_in_use(sid_x)) {
    fail("session_inject_state_unexpected");
    goto out;
  }

  /* Mint a delegated capability handle to `recipient`, parented on
   * the owner's broker port handle so the cascade subtree walker
   * (step 3) revokes it. The plan's §5.5 "delegated caps derived
   * from deleted owner become invalid" validation bullet calls out
   * gfx/input caps specifically, but `cap_handle_cap_id_valid`
   * currently caps the handle layer at CAP_IPC_RECV (14) — see
   * kernel/cap/cap_handle.c:47. Until CAP_GFX_FRAMEBUFFER (18) /
   * CAP_INPUT_* (19/20) are admitted to the handle table (#349 /
   * follow-up), we delegate CAP_FS_READ instead: the invariant the
   * §5.5 bullet pins is "a handle minted under the deleted owner's
   * subtree gates as CAP_ERR_MISSING after the cascade", which is
   * cap-id-agnostic. Mirrors the allow peer's recipient_h shape. */
  if (cap_table_grant(recipient, CAP_FS_READ) != CAP_OK) {
    fail("delegated_grant_setup_failed");
    goto out;
  }
  cap_handle_t gfx_h =
      cap_handle_grant_child(recipient, CAP_FS_READ,
                             sp.broker_handle);
  if (gfx_h == CAP_HANDLE_NULL) {
    fail("delegated_handle_mint_failed");
    goto out;
  }
  if (cap_gate_check_handle(gfx_h, CAP_FS_READ) != 1) {
    fail("delegated_handle_pre_gate");
    goto out;
  }

  /* Snapshot the audit ring baseline so the audit_wm_cascade_*
   * sub-checks key off NEW events emitted by this cascade only. */
  size_t audit_baseline = cap_audit_count_for_tests();

  /* Run the cascade with PCB teardown SUPPRESSED. Any session
   * teardown observable below is attributable to step 3.5, not to
   * the belt-and-suspenders cleanup inside process_destroy. */
  uint32_t n_children = 0u;
  broker_svc_result_t dr =
      broker_svc_delete_owner(owner, owner, sp.broker_handle,
                              PID_INVALID, &n_children);
  if (dr != BROKER_SVC_OK) {
    fail("broker_svc_delete_owner_not_ok");
    goto out;
  }
  if (n_children == 0u) {
    fail("cascade_count_zero");
    goto out;
  }

  /* Sub-check #1: both owner-scoped sessions destroyed. */
  if (sm_stub_in_use(sid_a) || sm_stub_in_use(sid_b)) {
    fail("owned_sessions_destroyed");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
         "owned_sessions_destroyed\n");

  /* Sub-check #2: bystander session untouched. The cascade's
   * subject filter is the load-bearing isolation guarantee. */
  if (!sm_stub_in_use(sid_x)) {
    fail("bystander_session_preserved");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
         "bystander_session_preserved\n");

  /* Sub-check #3: delegated gfx cap is invalid post-cascade. The
   * handle was minted parented under sp.broker_handle, so the
   * subtree walker (step 3) bumped its generation. CAP_ERR_MISSING
   * is the v0 spelling for "row was revoked / reaped" — same
   * translation the allow peer documents. */
  cap_result_t gcheck =
      cap_gate_check_handle_result(gfx_h, CAP_FS_READ);
  if (gcheck != CAP_ERR_MISSING) {
    fprintf(stderr,
            "post-cascade delegated gate_check_result = %d (want %d)\n",
            (int)gcheck, (int)CAP_ERR_MISSING);
    fail("delegated_gfx_caps_invalid");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
         "delegated_gfx_caps_invalid\n");

  /* Sub-check #4: session slot recyclable. A fresh inject of a new
   * session owned by the (still-live) owner subject must succeed —
   * the cascade freed the rows it tore down. */
  unsigned int sid_recycle = 0u;
  if (sm_stub_inject(owner, &sid_recycle) != 0) {
    fail("session_slot_recyclable");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
         "session_slot_recyclable\n");

  /* Sub-check #5: idempotent double-delete on the session leg.
   * Reset the freshly recycled session so the second cascade has no
   * owner-scoped sessions to drain — the session-leg `n_children`
   * contribution must be zero on this run. */
  sm_stub_destroy_count();  /* observation only; no-op */
  /* Clear the recycle row by destroying it directly so the next
   * cascade sees zero owner-scoped sessions. */
  {
    /* sm_stub does not expose a "destroy without going through the
     * cascade" hook — the public destroy *is* what we want here:
     * it's the same shim broker_svc.c calls in step 3.5. */
    extern void session_manager_destroy(unsigned int session_id);
    session_manager_destroy(sid_recycle);
  }
  if (sm_stub_in_use(sid_recycle)) {
    fail("session_recycle_cleanup_failed");
    goto out;
  }
  uint32_t n_children_2 = 0u;
  broker_svc_result_t dr2 =
      broker_svc_delete_owner(owner, owner, sp.broker_handle,
                              PID_INVALID, &n_children_2);
  if (dr2 != BROKER_SVC_OK) {
    fail("double_delete_idempotent_session_leg");
    goto out;
  }
  /* The cap leg's broker-share side-table was already drained on
   * the first cascade and the subtree walker's root is stale, so
   * n_children_2 should be 0 — every leg is idempotent. */
  if (n_children_2 != 0u) {
    fprintf(stderr,
            "double-delete n_children = %u (want 0)\n",
            (unsigned)n_children_2);
    fail("double_delete_idempotent_session_leg");
    goto out;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
         "double_delete_idempotent_session_leg\n");

  /* Sub-check #6 / #7: audit ring observed at least one
   * CAP_AUDIT_OP_CASCADE_REVOKE event whose subject_id is one of
   * the destroyed session ids (step 3.5 folds session ids into the
   * subject_id field per kernel/svc/broker_svc.c comment) and a
   * terminal CAP_AUDIT_OP_CASCADE_DONE for the owner. We scan only
   * the slice of the ring emitted by THIS cascade so we don't pick
   * up bystander audit traffic from unrelated grants. */
  {
    size_t total = cap_audit_count_for_tests();
    int saw_wm_cascade = 0;
    int saw_wm_done    = 0;
    for (size_t i = audit_baseline; i < total; ++i) {
      cap_audit_event_t ev;
      if (cap_audit_get_for_tests(i, &ev) != CAP_OK) continue;
      if (ev.operation == CAP_AUDIT_OP_CASCADE_REVOKE &&
          ev.actor_subject_id == owner &&
          ev.capability_id == (capability_id_t)0 &&
          (ev.subject_id == (cap_subject_id_t)sid_a ||
           ev.subject_id == (cap_subject_id_t)sid_b) &&
          ev.result == CAP_OK) {
        saw_wm_cascade = 1;
      }
      if (ev.operation == CAP_AUDIT_OP_CASCADE_DONE &&
          ev.actor_subject_id == owner &&
          ev.subject_id == owner &&
          ev.result == CAP_OK) {
        saw_wm_done = 1;
      }
    }
    if (!saw_wm_cascade) {
      fail("audit_wm_cascade_recorded");
      goto out;
    }
    printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
           "audit_wm_cascade_recorded\n");
    if (!saw_wm_done) {
      fail("audit_wm_cascade_done_recorded");
      goto out;
    }
    printf("TEST:PASS:m5_owner_delete_cascade_window_qemu:"
           "audit_wm_cascade_done_recorded\n");
  }

  /* Cleanup. Owner PCB was kept live above (PID_INVALID); tear it
   * down via the launcher so the spawn-table accounting stays
   * clean — mirrors the allow peer. */
  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m5_owner_delete_cascade_window_qemu\n");
  return 0;
}
