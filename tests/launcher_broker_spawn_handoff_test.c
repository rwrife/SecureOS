/**
 * @file launcher_broker_spawn_handoff_test.c
 * @brief Slice-2 acceptance test for plan #300 / issue #303
 *        (M4-SUBSTRATE-002).
 *
 * Asserts the M4 launcher-broker spawn handoff contract:
 *
 *   1. `launcher_broker_spawn_app_with_broker_cap(...)` returns a live
 *      PCB with a non-zero CAP_IPC_SEND handle stamped LE64 into
 *      `ipc_scratch[24..32)`. All other scratch bytes ([0..24) and
 *      [32..64)) remain zero.
 *   2. The minted handle resolves via `cap_handle_owner()` to the
 *      spawned subject and passes `cap_gate_check_handle()` for
 *      CAP_IPC_SEND. It does NOT gate for unrelated caps
 *      (CAP_CONSOLE_WRITE, CAP_FS_READ).
 *   3. `launcher_broker_spawn_destroy(pid)` invalidates the handle via
 *      the cap_handle_revoke_subject() cascade in `process_destroy()`.
 *
 * Output markers (consumed by build/scripts/test_launcher_broker_spawn_handoff.sh):
 *   TEST:PASS:launcher_broker_spawn_handoff_stamp
 *   TEST:PASS:launcher_broker_spawn_handoff_gate
 *   TEST:PASS:launcher_broker_spawn_handoff_revoke_on_destroy
 *   TEST:PASS:launcher_broker_spawn_handoff
 *
 * Pure host-side; no kernel runtime dependencies beyond the slice-2
 * sources and their transitive M1 substrate.
 *
 * Issue: #303. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md slice 2.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"
#include "../kernel/user/launcher.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_broker_spawn_handoff:%s\n", reason);
  g_fail = 1;
}

static cap_handle_t scratch_load_handle_le64(const uint8_t *p) {
  cap_handle_t lo = (cap_handle_t)p[0]
                  | ((cap_handle_t)p[1] <<  8)
                  | ((cap_handle_t)p[2] << 16)
                  | ((cap_handle_t)p[3] << 24);
  return lo;
}

static int scratch_top32_zero(const uint8_t *p) {
  return p[4] == 0u && p[5] == 0u && p[6] == 0u && p[7] == 0u;
}

static void test_setup(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
}

static void test_stamp(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_broker_spawn_t sp;
  launcher_result_t r = launcher_broker_spawn_app_with_broker_cap(&m, &sp);
  if (r != LAUNCHER_OK) {
    fail("stamp_spawn_returned_nonok");
    return;
  }
  if (sp.pid == PID_INVALID) {
    fail("stamp_invalid_pid");
    return;
  }
  if (sp.aspace == NULL || sp.aspace->ipc_scratch == NULL) {
    fail("stamp_null_aspace_or_scratch");
    return;
  }
  if (sp.broker_handle == CAP_HANDLE_NULL) {
    fail("stamp_null_broker_handle");
    return;
  }

  const uint8_t *p = (const uint8_t *)sp.aspace->ipc_scratch;

  /* Console + fs reserved slots [0..24) must be zero on a broker spawn. */
  for (size_t i = 0; i < 24; ++i) {
    if (p[i] != 0u) {
      fail("stamp_lower_slot_nonzero");
      return;
    }
  }

  /* [24..32): LE64(broker_handle). */
  if (scratch_load_handle_le64(&p[24]) != sp.broker_handle) {
    fail("stamp_scratch_broker_handle_mismatch");
    return;
  }
  if (!scratch_top32_zero(&p[24])) {
    fail("stamp_scratch_broker_top32_nonzero");
    return;
  }

  /* Reserved scratch bytes [32..64) must be zero. */
  for (size_t i = 32; i < 64; ++i) {
    if (p[i] != 0u) {
      fail("stamp_scratch_reserved_bytes_nonzero");
      return;
    }
  }

  printf("TEST:PASS:launcher_broker_spawn_handoff_stamp\n");
}

static void test_gate(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_broker_spawn_t sp;
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("gate_setup_failed");
    return;
  }

  if (cap_handle_owner(sp.broker_handle) != SUBJECT_M2_HELLOAPP) {
    fail("gate_owner_mismatch");
    return;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("gate_check_send_failed");
    return;
  }
  /* Cross-check: handle must NOT gate for unrelated caps. */
  if (cap_gate_check_handle(sp.broker_handle, CAP_CONSOLE_WRITE) != 0) {
    fail("gate_handle_passed_console");
    return;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_FS_READ) != 0) {
    fail("gate_handle_passed_fs_read");
    return;
  }

  /* Reject a non-zero auto_grant_count: the broker-spawn path must
   * refuse to smuggle in an unrelated grant. */
  capability_id_t extra = CAP_CONSOLE_WRITE;
  launcher_manifest_t bad = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = &extra,
      .auto_grant_count  = 1,
  };
  launcher_broker_spawn_t throwaway;
  if (launcher_broker_spawn_app_with_broker_cap(&bad, &throwaway)
      != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("gate_extra_auto_grant_not_rejected");
    return;
  }

  printf("TEST:PASS:launcher_broker_spawn_handoff_gate\n");
}

static void test_revoke_on_destroy(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_broker_spawn_t sp;
  if (launcher_broker_spawn_app_with_broker_cap(&m, &sp) != LAUNCHER_OK) {
    fail("revoke_setup_failed");
    return;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 1) {
    fail("revoke_pre_gate_check_failed");
    return;
  }

  if (launcher_broker_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("revoke_destroy_returned_nonok");
    return;
  }
  if (cap_gate_check_handle(sp.broker_handle, CAP_IPC_SEND) != 0) {
    fail("revoke_broker_handle_still_live");
    return;
  }

  /* PID_INVALID stays a no-op. */
  if (launcher_broker_spawn_destroy(PID_INVALID) != LAUNCHER_OK) {
    fail("revoke_pid_invalid_not_noop");
    return;
  }

  printf("TEST:PASS:launcher_broker_spawn_handoff_revoke_on_destroy\n");
}

int main(void) {
  test_stamp();
  test_gate();
  test_revoke_on_destroy();

  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:launcher_broker_spawn_handoff\n");
  return 0;
}
