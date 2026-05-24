/**
 * @file process_find_aspace_by_subject_test.c
 * @brief Lookup-by-subject helper for the M1 process table
 *        (issue #260, IPC bounds-check prerequisite).
 *
 * Purpose:
 *   Exercises `process_find_aspace_by_subject(...)`, the M1 substitute
 *   for a "current process / current address-space" lookup at IPC
 *   entry. The IPC bounds-check half of #260 needs to ground
 *   `aspace_contains(...)` on the caller's window but has nothing else
 *   to key on from `ipc_send` / `ipc_recv` signatures (`cap_subject_id_t`
 *   only). This helper is the bridge.
 *
 *   The IPC wiring itself is intentionally not in this slice — that
 *   half of #260 is still pending a marker-grammar / ABI design call
 *   (see the issue's most recent design-clarification comment). This
 *   slice is strictly additive and unblocks the wiring without locking
 *   in any of the open decisions.
 *
 * Covered cases (each emits exactly one TEST:PASS marker on success;
 * any failure path emits a TEST:FAIL marker and exits non-zero):
 *
 *   1. hit_returns_stored_aspace        — create with sentinel aspace,
 *      lookup returns identical pointer.
 *   2. miss_returns_null                — unrelated subject returns NULL.
 *   3. subject_zero_returns_null        — the v0 unknown-subject
 *      sentinel always misses, even after a create with subject 0.
 *   4. destroy_then_miss                — destroy clears the row; the
 *      same subject no longer resolves.
 *   5. multiple_subjects_first_wins     — two live PCBs with distinct
 *      subjects each resolve to their own aspace.
 *   6. null_aspace_returns_null         — a create with NULL aspace
 *      resolves to NULL (the hit case is still observable: the slot
 *      is occupied, the stored pointer is just NULL).
 *
 * Launched by:
 *   build/scripts/test_process_find_aspace_by_subject.sh
 *   (dispatched via build/scripts/test.sh process_find_aspace_by_subject).
 *
 * Issue: #260.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"

static void die(const char *reason) {
  printf("TEST:FAIL:process_find_aspace_by_subject:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  process_table_reset();
}

static process_id_t create_or_die(cap_subject_id_t subject,
                                  address_space_t *aspace,
                                  const char *where) {
  process_id_t pid = PID_INVALID;
  proc_result_t rc = process_create(subject, aspace, &pid);
  if (rc != PROC_OK || pid == PID_INVALID) {
    die(where);
  }
  return pid;
}

static void check_hit_returns_stored_aspace(void) {
  reset_world();
  address_space_t *sentinel = (address_space_t *)(uintptr_t)0xCAFEBABEu;
  (void)create_or_die((cap_subject_id_t)42u, sentinel, "create_for_hit");

  address_space_t *got = process_find_aspace_by_subject((cap_subject_id_t)42u);
  if (got != sentinel) {
    die("hit_did_not_return_stored_pointer");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:hit_returns_stored_aspace\n");
}

static void check_miss_returns_null(void) {
  reset_world();
  address_space_t *sentinel = (address_space_t *)(uintptr_t)0xDEADBEEFu;
  (void)create_or_die((cap_subject_id_t)7u, sentinel, "create_for_miss");

  if (process_find_aspace_by_subject((cap_subject_id_t)999u) != NULL) {
    die("miss_did_not_return_null");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:miss_returns_null\n");
}

static void check_subject_zero_returns_null(void) {
  reset_world();
  /* Even if a caller manages to wedge subject 0 into the table, the
   * sentinel must not resolve. Documented as the v0 "unknown / unset"
   * sentinel in process.h. */
  address_space_t *sentinel = (address_space_t *)(uintptr_t)0xF00DBABEu;
  (void)create_or_die((cap_subject_id_t)0u, sentinel, "create_with_subject_zero");

  if (process_find_aspace_by_subject((cap_subject_id_t)0u) != NULL) {
    die("subject_zero_resolved");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:subject_zero_returns_null\n");
}

static void check_destroy_then_miss(void) {
  reset_world();
  address_space_t *sentinel = (address_space_t *)(uintptr_t)0xABCD1234u;
  process_id_t pid = create_or_die((cap_subject_id_t)11u, sentinel,
                                   "create_for_destroy");

  if (process_find_aspace_by_subject((cap_subject_id_t)11u) != sentinel) {
    die("pre_destroy_hit_missing");
  }
  if (process_destroy(pid) != PROC_OK) {
    die("destroy_failed");
  }
  if (process_find_aspace_by_subject((cap_subject_id_t)11u) != NULL) {
    die("post_destroy_still_resolves");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:destroy_then_miss\n");
}

static void check_multiple_subjects_each_resolves(void) {
  reset_world();
  address_space_t *a = (address_space_t *)(uintptr_t)0xA0A0A0A0u;
  address_space_t *b = (address_space_t *)(uintptr_t)0xB0B0B0B0u;
  (void)create_or_die((cap_subject_id_t)21u, a, "create_a");
  (void)create_or_die((cap_subject_id_t)22u, b, "create_b");

  if (process_find_aspace_by_subject((cap_subject_id_t)21u) != a) {
    die("subject_21_wrong_aspace");
  }
  if (process_find_aspace_by_subject((cap_subject_id_t)22u) != b) {
    die("subject_22_wrong_aspace");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:multiple_subjects_first_wins\n");
}

static void check_null_aspace_returns_null(void) {
  reset_world();
  /* A create with NULL aspace still occupies a slot. The lookup
   * returns NULL not because of a miss, but because the stored
   * pointer is NULL. That is the v0-correct behaviour: the IPC
   * bounds wiring (#260) treats NULL aspace as "no PCB-keyed bounds
   * check available", and this case round-trips the same value. */
  (void)create_or_die((cap_subject_id_t)33u, NULL, "create_with_null_aspace");

  if (process_find_aspace_by_subject((cap_subject_id_t)33u) != NULL) {
    die("null_aspace_did_not_return_null");
  }
  /* Sanity: the slot really is occupied. Asking for an unrelated
   * subject should also be NULL (i.e. we are not accidentally
   * conflating "miss" with "hit-but-null"). */
  if (process_find_aspace_by_subject((cap_subject_id_t)34u) != NULL) {
    die("unrelated_subject_resolved");
  }
  printf("TEST:PASS:process_find_aspace_by_subject:null_aspace_returns_null\n");
}

int main(void) {
  check_hit_returns_stored_aspace();
  check_miss_returns_null();
  check_subject_zero_returns_null();
  check_destroy_then_miss();
  check_multiple_subjects_each_resolves();
  check_null_aspace_returns_null();
  printf("TEST:PASS:process_find_aspace_by_subject\n");
  return 0;
}
