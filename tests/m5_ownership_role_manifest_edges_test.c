/* tests/m5_ownership_role_manifest_edges_test.c
 *
 * Host-side acceptance for issue #585 ownership-role launcher wiring.
 *
 * Purpose:
 *   Verifies that `launcher_broker_spawn_app_with_broker_cap()` consumes
 *   `launcher_manifest_t::ownership_role` and records the expected
 *   parent-edge behavior for the spawned broker handle:
 *
 *     - OWNER    -> child edge rooted at launcher handle
 *     - DELEGATE -> child edge rooted at launcher handle
 *     - NONE     -> legacy sentinel root (no parent edge)
 *
 * Evidence model:
 *   We root a subtree revoke at the launcher's broker handle and inspect
 *   whether the spawned app's broker handle becomes stale.
 *
 * Called by:
 *   build/scripts/test_launcher_ownership_role_manifest_edges.sh
 */

#include <stdio.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"
#include "../kernel/user/launcher.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_ownership_role_manifest_edges:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
}

static cap_handle_t seed_launcher_root_handle(void) {
  if (cap_table_grant((cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                      CAP_IPC_SEND) != CAP_OK) {
    return CAP_HANDLE_NULL;
  }
  return cap_handle_grant((cap_subject_id_t)SUBJECT_M2_LAUNCHER,
                          CAP_IPC_SEND);
}

static void run_edge_case(launcher_ownership_role_t role,
                          int expect_revoked,
                          const char *pass_marker) {
  reset_world();

  cap_handle_t launcher_root = seed_launcher_root_handle();
  if (launcher_root == CAP_HANDLE_NULL) {
    fail("launcher_root_seed_failed");
    return;
  }

  launcher_manifest_t m = {
      .subject_id = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
      .auto_grant_caps = NULL,
      .auto_grant_count = 0u,
      .arena_bytes = 0u,
      .ownership_role = role,
  };

  launcher_broker_spawn_t sp;
  memset(&sp, 0, sizeof sp);
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("broker_spawn_failed");
    return;
  }
  if (sp.pid == PID_INVALID || sp.broker_handle == CAP_HANDLE_NULL) {
    fail("broker_spawn_bad_output");
    return;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("broker_handle_pre_gate_failed");
    return;
  }

  if (cap_handle_revoke_subtree(launcher_root) != CAP_OK) {
    fail("launcher_root_cascade_failed");
    return;
  }

  cap_result_t post = cap_gate_check_handle_result(sp.broker_handle,
                                                    CAP_IPC_SEND);
  if (expect_revoked) {
    if (post != CAP_ERR_MISSING) {
      fail("expected_revoked_handle_after_launcher_cascade");
      return;
    }
  } else {
    if (post != CAP_OK) {
      fail("expected_handle_to_remain_live_for_none_role");
      return;
    }
  }

  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("broker_spawn_destroy_failed");
    return;
  }

  printf("TEST:PASS:%s\n", pass_marker);
}

int main(void) {
  run_edge_case(LAUNCHER_OWNERSHIP_ROLE_OWNER,
                /*expect_revoked=*/1,
                "launcher_ownership_role_owner_registers_edge");

  run_edge_case(LAUNCHER_OWNERSHIP_ROLE_DELEGATE,
                /*expect_revoked=*/1,
                "launcher_ownership_role_delegate_registers_edge");

  run_edge_case(LAUNCHER_OWNERSHIP_ROLE_NONE,
                /*expect_revoked=*/0,
                "launcher_ownership_role_none_registers_no_edge");

  if (g_fail) {
    return 1;
  }
  puts("TEST:PASS:launcher_ownership_role_manifest_edges");
  return 0;
}
