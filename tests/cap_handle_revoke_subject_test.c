/**
 * @file cap_handle_revoke_subject_test.c
 * @brief Unit tests for the M1-CAPTBL-003 bulk-revoke-by-owner entry
 *        point and the process_destroy() hook that calls it (issue
 *        #239, plan plans/2026-05-20-m1-kernel-capability-table.md).
 *
 * Cases:
 *   1. Three caps granted to one owner -> cap_handle_revoke_subject
 *      returns 3, each prior handle now denies with CAP_ERR_MISSING,
 *      and live_count drops to 0.
 *   2. Two owners, two caps each -> revoking owner-A leaves owner-B's
 *      handles passing the gate; live_count reflects the half-table
 *      state.
 *   3. Revoke on an owner with zero live rows returns 0 (no-op).
 *   4. Revoke on an out-of-range subject id returns 0 (bounds check).
 *   5. Integration: process_create(subject) + cap_handle_grant(subject)
 *      + process_destroy(pid) -> the previously-issued handle denies
 *      with CAP_ERR_MISSING. Exercises the process.c hook end-to-end.
 *
 * Launched by:
 *   build/scripts/test_cap_handle_revoke_subject.sh (registered with
 *   build/scripts/test.sh under the `cap_handle_revoke_subject` target).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_handle_revoke_subject:%s\n", reason);
  exit(1);
}

static void test_bulk_revoke_one_owner(void) {
  cap_handle_table_reset();

  cap_handle_t h_console = cap_handle_grant((cap_subject_id_t)2, CAP_CONSOLE_WRITE);
  cap_handle_t h_serial  = cap_handle_grant((cap_subject_id_t)2, CAP_SERIAL_WRITE);
  cap_handle_t h_fs      = cap_handle_grant((cap_subject_id_t)2, CAP_FS_WRITE);
  if (h_console == CAP_HANDLE_NULL || h_serial == CAP_HANDLE_NULL || h_fs == CAP_HANDLE_NULL) {
    fail("grant_null");
  }
  if (!cap_gate_check_handle(h_console, CAP_CONSOLE_WRITE) ||
      !cap_gate_check_handle(h_serial,  CAP_SERIAL_WRITE) ||
      !cap_gate_check_handle(h_fs,      CAP_FS_WRITE)) {
    fail("pre_revoke_check");
  }
  if (cap_handle_table_live_count() != 3u) {
    fail("pre_revoke_live_count");
  }

  uint32_t revoked = cap_handle_revoke_subject((cap_subject_id_t)2);
  if (revoked != 3u) {
    fail("revoke_count");
  }
  if (cap_handle_table_live_count() != 0u) {
    fail("post_revoke_live_count");
  }

  if (cap_gate_check_handle_result(h_console, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("stale_handle_console");
  }
  if (cap_gate_check_handle_result(h_serial, CAP_SERIAL_WRITE) != CAP_ERR_MISSING) {
    fail("stale_handle_serial");
  }
  if (cap_gate_check_handle_result(h_fs, CAP_FS_WRITE) != CAP_ERR_MISSING) {
    fail("stale_handle_fs");
  }

  printf("TEST:PASS:cap_handle_revoke_subject_bulk_one_owner\n");
}

static void test_revoke_isolates_owners(void) {
  cap_handle_table_reset();

  cap_handle_t a1 = cap_handle_grant((cap_subject_id_t)3, CAP_CONSOLE_WRITE);
  cap_handle_t a2 = cap_handle_grant((cap_subject_id_t)3, CAP_SERIAL_WRITE);
  cap_handle_t b1 = cap_handle_grant((cap_subject_id_t)4, CAP_CONSOLE_WRITE);
  cap_handle_t b2 = cap_handle_grant((cap_subject_id_t)4, CAP_FS_WRITE);
  if (a1 == CAP_HANDLE_NULL || a2 == CAP_HANDLE_NULL ||
      b1 == CAP_HANDLE_NULL || b2 == CAP_HANDLE_NULL) {
    fail("grant_two_owners");
  }
  if (cap_handle_table_live_count() != 4u) {
    fail("two_owner_initial_live_count");
  }

  uint32_t revoked = cap_handle_revoke_subject((cap_subject_id_t)3);
  if (revoked != 2u) {
    fail("isolated_revoke_count");
  }
  if (cap_handle_table_live_count() != 2u) {
    fail("post_isolated_live_count");
  }

  if (cap_gate_check_handle(a1, CAP_CONSOLE_WRITE) ||
      cap_gate_check_handle(a2, CAP_SERIAL_WRITE)) {
    fail("owner_a_handles_still_pass");
  }
  if (!cap_gate_check_handle(b1, CAP_CONSOLE_WRITE) ||
      !cap_gate_check_handle(b2, CAP_FS_WRITE)) {
    fail("owner_b_handles_did_not_survive");
  }

  printf("TEST:PASS:cap_handle_revoke_subject_isolates_owners\n");
}

static void test_revoke_no_rows(void) {
  cap_handle_table_reset();

  if (cap_handle_revoke_subject((cap_subject_id_t)5) != 0u) {
    fail("empty_owner_nonzero");
  }
  /* Grant for one subject; revoke a different one. */
  cap_handle_t h = cap_handle_grant((cap_subject_id_t)1, CAP_CONSOLE_WRITE);
  if (h == CAP_HANDLE_NULL) {
    fail("grant_for_other");
  }
  if (cap_handle_revoke_subject((cap_subject_id_t)6) != 0u) {
    fail("other_owner_nonzero");
  }
  if (!cap_gate_check_handle(h, CAP_CONSOLE_WRITE)) {
    fail("other_owner_revoke_disturbed_live_row");
  }

  printf("TEST:PASS:cap_handle_revoke_subject_no_rows\n");
}

static void test_revoke_bad_subject(void) {
  cap_handle_table_reset();
  /* CAP_TABLE_MAX_SUBJECTS == 8 at OS_ABI_VERSION=0. */
  if (cap_handle_revoke_subject((cap_subject_id_t)99) != 0u) {
    fail("out_of_range_subject_revoked_rows");
  }
  printf("TEST:PASS:cap_handle_revoke_subject_bad_subject\n");
}

static void test_process_destroy_revokes_caps(void) {
  cap_handle_table_reset();
  process_table_reset();

  process_id_t pid = PID_INVALID;
  if (process_create((cap_subject_id_t)2, NULL, &pid) != PROC_OK) {
    fail("process_create");
  }
  cap_handle_t h = cap_handle_grant((cap_subject_id_t)2, CAP_CONSOLE_WRITE);
  if (h == CAP_HANDLE_NULL) {
    fail("integ_grant_null");
  }
  if (!cap_gate_check_handle(h, CAP_CONSOLE_WRITE)) {
    fail("integ_pre_destroy_check");
  }

  if (process_destroy(pid) != PROC_OK) {
    fail("process_destroy");
  }
  if (cap_gate_check_handle_result(h, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("integ_post_destroy_handle_not_stale");
  }
  if (cap_handle_table_live_count() != 0u) {
    fail("integ_post_destroy_live_count");
  }

  /* Re-create a fresh process for a *different* subject; its grant must
   * not be touched by an unrelated destroy. */
  process_id_t pid2 = PID_INVALID;
  if (process_create((cap_subject_id_t)3, NULL, &pid2) != PROC_OK) {
    fail("process_create_b");
  }
  cap_handle_t h2 = cap_handle_grant((cap_subject_id_t)3, CAP_FS_WRITE);
  if (h2 == CAP_HANDLE_NULL) {
    fail("integ_grant_b_null");
  }
  /* Destroying pid2 stales h2 but must not affect any prior subject. */
  if (process_destroy(pid2) != PROC_OK) {
    fail("process_destroy_b");
  }
  if (cap_gate_check_handle_result(h2, CAP_FS_WRITE) != CAP_ERR_MISSING) {
    fail("integ_pid2_handle_not_stale");
  }

  printf("TEST:PASS:cap_handle_revoke_subject_process_destroy_hook\n");
}

int main(void) {
  test_bulk_revoke_one_owner();
  test_revoke_isolates_owners();
  test_revoke_no_rows();
  test_revoke_bad_subject();
  test_process_destroy_revokes_caps();
  printf("TEST:PASS:cap_handle_revoke_subject\n");
  return 0;
}
