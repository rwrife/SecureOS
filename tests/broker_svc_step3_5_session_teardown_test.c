/**
 * @file broker_svc_step3_5_session_teardown_test.c
 * @brief Validator for the M5-SUBSTRATE-005b step-3.5 WM-session
 *        teardown leg of the BROKER_OP_DELETE_OWNER cascade
 *        (issue #350, plan plans/2026-05-26-m5-wm-cascade-on-substrate.md).
 *
 * Asserts the contract documented in `kernel/svc/broker_svc.h`:
 *
 *   - When the owner subject owns zero WM sessions at cascade time,
 *     step 3.5 is a zero-iteration loop and `*out_n` equals only the
 *     cap-leg count (the existing #324 contract).
 *   - When the owner subject owns one WM session, step 3.5 calls
 *     `session_manager_destroy` exactly once and adds 1 to `*out_n`.
 *   - When the owner subject owns three WM sessions, step 3.5
 *     destroys all three and adds 3 to `*out_n`. After the cascade
 *     returns, `session_manager_first_session_for_subject(owner, ...)`
 *     misses (returns -1) for the owner subject \u2014 the load-bearing
 *     post-condition.
 *   - Sessions owned by an unrelated subject are NOT torn down by
 *     the cascade (isolation guarantee).
 *
 * Ordering invariant (broker_svc.h doc-block):
 *   step 3.5 runs AFTER step 3 (subtree revoke) and BEFORE step 4
 *   (process_destroy). This test passes `PID_INVALID` so step 4 is a
 *   no-op; step 3.5 is the focus.
 *
 * Output markers (consumed by
 * build/scripts/test_broker_svc_step3_5_session_teardown.sh):
 *   TEST:PASS:broker_svc_step3_5_no_session_owner
 *   TEST:PASS:broker_svc_step3_5_single_session_destroyed
 *   TEST:PASS:broker_svc_step3_5_multiple_sessions_destroyed
 *   TEST:PASS:broker_svc_step3_5_post_enumerator_misses
 *   TEST:PASS:broker_svc_step3_5_unrelated_subject_isolated
 *   TEST:PASS:broker_svc_step3_5_cascade_count_includes_sessions
 *   TEST:PASS:broker_svc_step3_5_session_teardown
 *
 * Pure host. session_manager is the harness stub in
 * tests/harness/session_manager_stub.{c,h}, not the kernel
 * implementation.
 *
 * Issue: #350. Plan: plans/2026-05-26-m5-wm-cascade-on-substrate.md
 * slice 005b.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_broker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/core/session_manager.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/svc/broker_svc.h"
#include "harness/session_manager_stub.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:broker_svc_step3_5_session_teardown:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  broker_svc_reset();
  cap_broker_reset();
  cap_handle_table_reset();
  ipc_port_table_reset();
  cap_table_reset();
  sm_stub_reset();
}

/* Bring up broker_svc + mint the owner's broker-port handle.
 * Returns the minted handle, or CAP_HANDLE_NULL on setup failure. */
static cap_handle_t setup_owner_handle(cap_subject_id_t owner) {
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("broker_svc_init");
    return CAP_HANDLE_NULL;
  }
  if (cap_table_grant(owner, CAP_IPC_SEND) != CAP_OK) {
    fail("owner_grant_setup");
    return CAP_HANDLE_NULL;
  }
  cap_handle_t h = cap_handle_grant(owner, CAP_IPC_SEND);
  if (h == CAP_HANDLE_NULL) {
    fail("owner_broker_handle_mint");
  }
  return h;
}

int main(void) {
  const cap_subject_id_t owner      = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  const cap_subject_id_t bystander  = (cap_subject_id_t)9001u;

  /* --- Case 1: owner has zero WM sessions. Step 3.5 is a no-op. --- */
  reset_world();
  {
    cap_handle_t owner_h = setup_owner_handle(owner);
    if (owner_h == CAP_HANDLE_NULL) goto out;

    uint32_t n = 0u;
    broker_svc_result_t rc = broker_svc_delete_owner(owner, owner, owner_h,
                                                     PID_INVALID, &n);
    if (rc != BROKER_SVC_OK) {
      fail("case1_delete_owner_not_ok");
      goto out;
    }
    if (n != 0u) {
      fprintf(stderr, "case1 n=%u (want 0)\n", (unsigned)n);
      fail("case1_n_not_zero");
      goto out;
    }
    if (sm_stub_destroy_count() != 0u) {
      fail("case1_unexpected_destroy");
      goto out;
    }
  }
  printf("TEST:PASS:broker_svc_step3_5_no_session_owner\n");

  /* --- Case 2: owner has exactly one WM session. --- */
  reset_world();
  {
    cap_handle_t owner_h = setup_owner_handle(owner);
    if (owner_h == CAP_HANDLE_NULL) goto out;

    unsigned int sid_a = 0u;
    if (sm_stub_inject(owner, &sid_a) != 0) {
      fail("case2_inject_session");
      goto out;
    }

    uint32_t n = 0u;
    if (broker_svc_delete_owner(owner, owner, owner_h,
                                PID_INVALID, &n) != BROKER_SVC_OK) {
      fail("case2_delete_owner_not_ok");
      goto out;
    }
    if (sm_stub_destroy_count() != 1u) {
      fprintf(stderr, "case2 destroy_count=%u (want 1)\n",
              sm_stub_destroy_count());
      fail("case2_destroy_count_wrong");
      goto out;
    }
    if (sm_stub_in_use(sid_a)) {
      fail("case2_session_still_in_use");
      goto out;
    }
    if (n != 1u) {
      fprintf(stderr, "case2 n=%u (want 1)\n", (unsigned)n);
      fail("case2_count_wrong");
      goto out;
    }
  }
  printf("TEST:PASS:broker_svc_step3_5_single_session_destroyed\n");

  /* --- Case 3: owner has three WM sessions; bystander has one
   *             (isolation control). --- */
  reset_world();
  {
    cap_handle_t owner_h = setup_owner_handle(owner);
    if (owner_h == CAP_HANDLE_NULL) goto out;

    unsigned int o1 = 0u, o2 = 0u, o3 = 0u, b1 = 0u;
    if (sm_stub_inject(owner,     &o1) != 0 ||
        sm_stub_inject(bystander, &b1) != 0 ||
        sm_stub_inject(owner,     &o2) != 0 ||
        sm_stub_inject(owner,     &o3) != 0) {
      fail("case3_inject_sessions");
      goto out;
    }

    uint32_t n = 0u;
    if (broker_svc_delete_owner(owner, owner, owner_h,
                                PID_INVALID, &n) != BROKER_SVC_OK) {
      fail("case3_delete_owner_not_ok");
      goto out;
    }

    if (sm_stub_destroy_count() != 3u) {
      fprintf(stderr, "case3 destroy_count=%u (want 3)\n",
              sm_stub_destroy_count());
      fail("case3_destroy_count_wrong");
      goto out;
    }
    if (sm_stub_in_use(o1) || sm_stub_in_use(o2) || sm_stub_in_use(o3)) {
      fail("case3_owner_session_survived");
      goto out;
    }
    printf("TEST:PASS:broker_svc_step3_5_multiple_sessions_destroyed\n");

    /* Post-condition: enumerator now misses for the owner subject. */
    unsigned int probe = 0xFFFFFFFFu;
    if (session_manager_first_session_for_subject(owner, &probe) != -1) {
      fail("case3_enumerator_did_not_miss");
      goto out;
    }
    printf("TEST:PASS:broker_svc_step3_5_post_enumerator_misses\n");

    /* Isolation: the bystander's session is untouched. */
    if (!sm_stub_in_use(b1)) {
      fail("case3_bystander_session_destroyed");
      goto out;
    }
    unsigned int b_probe = 0xFFFFFFFFu;
    if (session_manager_first_session_for_subject(bystander, &b_probe) != 0 ||
        b_probe != b1) {
      fail("case3_bystander_enumerator_changed");
      goto out;
    }
    printf("TEST:PASS:broker_svc_step3_5_unrelated_subject_isolated\n");

    if (n != 3u) {
      fprintf(stderr, "case3 n=%u (want 3)\n", (unsigned)n);
      fail("case3_count_wrong");
      goto out;
    }
    printf("TEST:PASS:broker_svc_step3_5_cascade_count_includes_sessions\n");
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:broker_svc_step3_5_session_teardown\n");
  return 0;
}
