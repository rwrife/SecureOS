/**
 * @file cap_table_skeleton_test.c
 * @brief Unit tests for the M1 capability handle table skeleton (#225).
 *
 * Purpose:
 *   Exercises `kernel/cap/cap_handle.{c,h}` against the four cases listed
 *   in issue #225's "Done when":
 *     1. Grant → check passes; revoke → check fails.
 *     2. Duplicate grant is idempotent (no double-count, no spurious err).
 *     3. Revoke of an ungranted (subject, cap) returns CAP_ERR_MISSING.
 *     4. Table-full rejection.
 *
 *   Plus a small invariant check that the bounded table really is bounded
 *   (live count never exceeds CAP_HANDLE_TABLE_MAX).
 *
 * Interactions:
 *   - cap_handle.c: the unit under test.
 *   - capability.h: shared `capability_id_t` / `cap_result_t` vocabulary.
 *
 * Launched by:
 *   build/scripts/test_cap_table_skeleton.sh (registered with
 *   build/scripts/test.sh under the `cap_table_skeleton` target).
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_table_skeleton:%s\n", reason);
  exit(1);
}

static void test_grant_check_revoke(void) {
  cap_handle_table_reset();

  if (cap_handle_table_check(2u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny_before_grant");
  }
  if (cap_handle_table_grant(2u, 1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("first_grant_failed");
  }
  if (cap_handle_table_check(2u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("check_after_grant");
  }
  if (cap_handle_table_revoke(2u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_failed");
  }
  if (cap_handle_table_check(2u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("check_after_revoke");
  }
  printf("TEST:PASS:cap_table_skeleton_grant_check_revoke\n");
}

static void test_grant_idempotent(void) {
  cap_handle_table_reset();

  if (cap_handle_table_grant(3u, 1u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("idem_first_grant");
  }
  const uint32_t after_first = cap_handle_table_live_count();
  if (after_first != 1u) {
    fail("idem_first_grant_count");
  }
  /* Duplicate grants — same (owner, cap), possibly different granter. Both
   * MUST return CAP_OK and the live-row count MUST stay at 1. */
  if (cap_handle_table_grant(3u, 1u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("idem_dup_grant_same_granter");
  }
  if (cap_handle_table_grant(3u, 4u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("idem_dup_grant_other_granter");
  }
  if (cap_handle_table_live_count() != after_first) {
    fail("idem_double_count");
  }
  if (cap_handle_table_check(3u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("idem_check_after_dup");
  }
  printf("TEST:PASS:cap_table_skeleton_grant_idempotent\n");
}

static void test_revoke_ungranted(void) {
  cap_handle_table_reset();

  /* Never granted — revoke MUST report MISSING and MUST NOT mutate state. */
  if (cap_handle_table_revoke(5u, CAP_DEBUG_EXIT) != CAP_ERR_MISSING) {
    fail("revoke_ungranted_wrong_result");
  }
  if (cap_handle_table_live_count() != 0u) {
    fail("revoke_ungranted_mutated_table");
  }
  if (cap_handle_table_check(5u, CAP_DEBUG_EXIT) != CAP_ERR_MISSING) {
    fail("revoke_ungranted_check_drift");
  }

  /* Grant then revoke twice: second revoke MUST also report MISSING. */
  if (cap_handle_table_grant(5u, 1u, CAP_DEBUG_EXIT) != CAP_OK) {
    fail("revoke_ungranted_pre_grant");
  }
  if (cap_handle_table_revoke(5u, CAP_DEBUG_EXIT) != CAP_OK) {
    fail("revoke_ungranted_first_revoke");
  }
  if (cap_handle_table_revoke(5u, CAP_DEBUG_EXIT) != CAP_ERR_MISSING) {
    fail("revoke_ungranted_double_revoke");
  }
  printf("TEST:PASS:cap_table_skeleton_revoke_ungranted\n");
}

static void test_table_full_rejection(void) {
  cap_handle_table_reset();

  /* We have 7 valid subjects (0..CAP_TABLE_MAX_SUBJECTS-1=7) and
   * CAP_IPC_RECV - CAP_CONSOLE_WRITE + 1 = 14 valid caps. That gives a
   * worst-case 7*14 = 98 distinct (subject, cap) pairs, comfortably above
   * CAP_HANDLE_TABLE_MAX=64. We fill the table by walking the cap space
   * across subjects and assert that the (CAP_HANDLE_TABLE_MAX+1)-th grant
   * returns CAP_ERR_CAP_INVALID (the "table full" signal in v0). */
  uint32_t granted = 0u;
  for (cap_subject_id_t subject = 0; subject < CAP_TABLE_MAX_SUBJECTS; ++subject) {
    for (capability_id_t cap = CAP_CONSOLE_WRITE; cap <= CAP_IPC_RECV;
         cap = (capability_id_t)(cap + 1)) {
      cap_result_t r = cap_handle_table_grant(subject, 1u, cap);
      if (granted < CAP_HANDLE_TABLE_MAX) {
        if (r != CAP_OK) {
          fail("table_full_premature_reject");
        }
        granted += 1u;
      } else {
        if (r != CAP_ERR_CAP_INVALID) {
          fail("table_full_did_not_reject");
        }
        if (cap_handle_table_live_count() != CAP_HANDLE_TABLE_MAX) {
          fail("table_full_overflowed_bound");
        }
        printf("TEST:PASS:cap_table_skeleton_table_full\n");
        return;
      }
    }
  }
  fail("table_full_never_filled");
}

static void test_bounds_invariants(void) {
  cap_handle_table_reset();

  /* Bad subject. */
  if (cap_handle_table_grant(CAP_TABLE_MAX_SUBJECTS, 1u, CAP_CONSOLE_WRITE) !=
      CAP_ERR_SUBJECT_INVALID) {
    fail("bounds_bad_subject_grant");
  }
  if (cap_handle_table_revoke(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE) !=
      CAP_ERR_SUBJECT_INVALID) {
    fail("bounds_bad_subject_revoke");
  }
  if (cap_handle_table_check(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE) !=
      CAP_ERR_SUBJECT_INVALID) {
    fail("bounds_bad_subject_check");
  }

  /* Bad cap id (0 is below the documented CAP_CONSOLE_WRITE = 1 floor). */
  if (cap_handle_table_grant(0u, 1u, (capability_id_t)0) != CAP_ERR_CAP_INVALID) {
    fail("bounds_bad_cap_grant");
  }
  if (cap_handle_table_check(0u, (capability_id_t)999) != CAP_ERR_CAP_INVALID) {
    fail("bounds_bad_cap_check");
  }
  printf("TEST:PASS:cap_table_skeleton_bounds\n");
}

int main(void) {
  printf("TEST:START:cap_table_skeleton\n");
  test_grant_check_revoke();
  test_grant_idempotent();
  test_revoke_ungranted();
  test_table_full_rejection();
  test_bounds_invariants();
  printf("TEST:PASS:cap_table_skeleton\n");
  return 0;
}
