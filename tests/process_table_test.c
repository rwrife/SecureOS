/**
 * @file process_table_test.c
 * @brief Lifecycle + table-occupancy tests for the M1 process table
 *        (issue #224).
 *
 * Asserts the invariants spelled out in issue #224's scope:
 *
 *   1. process_create → process_lookup returns the same subject/aspace
 *      the caller supplied, and a non-zero PID.
 *   2. process_destroy invalidates the PID: lookup + double-destroy
 *      return PROC_ERR_INVALID_PID; destroy(PID_INVALID) too.
 *   3. process_create → destroy → create on an otherwise-empty table
 *      reuses the same slot index but advances the generation half of
 *      the PID (no stale-handle aliasing — matches the #220 / #237
 *      lifecycle pattern).
 *   4. Filling the table to PROC_TABLE_MAX returns PROC_OK for every
 *      slot; the next create returns PROC_ERR_TABLE_FULL and writes
 *      PID_INVALID to the out param.
 *   5. process_table_reset invalidates every previously issued PID
 *      (generation bump on reset).
 *
 * Launched by:
 *   build/scripts/test_process_table.sh (dispatched via
 *   build/scripts/test.sh process_table).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"

static void die(const char *reason) {
  printf("TEST:FAIL:process_table:%s\n", reason);
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
  if (rc != PROC_OK) {
    die(where);
  }
  if (pid == PID_INVALID) {
    die("create_returned_invalid_pid");
  }
  return pid;
}

/* Check 1: round-trip create + lookup preserves subject + aspace. */
static void check_create_lookup_roundtrip(void) {
  reset_world();
  /* Use a distinctive sentinel pointer value (cast from an integer
   * literal) so we can prove process_create stored it verbatim
   * without dereferencing it. v0 treats address_space_t * as opaque. */
  address_space_t *sentinel = (address_space_t *)(uintptr_t)0xCAFEBABEu;
  process_id_t pid = create_or_die((cap_subject_id_t)42u, sentinel,
                                   "create_roundtrip");

  process_t snap;
  memset(&snap, 0, sizeof(snap));
  if (process_lookup(pid, &snap) != PROC_OK) {
    die("lookup_after_create_failed");
  }
  if (snap.pid != pid) {
    die("snapshot_pid_mismatch");
  }
  if (snap.subject != (cap_subject_id_t)42u) {
    die("snapshot_subject_mismatch");
  }
  if (snap.aspace != sentinel) {
    die("snapshot_aspace_mismatch");
  }
  if (!process_is_live_for_tests(pid)) {
    die("live_check_false_for_live_pid");
  }

  /* lookup with NULL out-param is PROC_ERR_INVALID_ARG. */
  if (process_lookup(pid, NULL) != PROC_ERR_INVALID_ARG) {
    die("lookup_null_outparam_not_rejected");
  }

  if (process_destroy(pid) != PROC_OK) {
    die("cleanup_destroy_failed");
  }
  printf("TEST:PASS:process_table_create_lookup\n");
}

/* Check 2: destroy semantics. Stale lookup, double-destroy, and
 * destroy(PID_INVALID) all return PROC_ERR_INVALID_PID. */
static void check_destroy_semantics(void) {
  reset_world();
  process_id_t pid = create_or_die((cap_subject_id_t)7u, NULL, "destroy_create");

  if (process_destroy(pid) != PROC_OK) {
    die("first_destroy_failed");
  }

  process_t snap;
  memset(&snap, 0, sizeof(snap));
  if (process_lookup(pid, &snap) != PROC_ERR_INVALID_PID) {
    die("stale_lookup_not_rejected");
  }
  if (process_is_live_for_tests(pid)) {
    die("live_check_true_for_destroyed_pid");
  }
  if (process_destroy(pid) != PROC_ERR_INVALID_PID) {
    die("double_destroy_not_rejected");
  }
  if (process_destroy(PID_INVALID) != PROC_ERR_INVALID_PID) {
    die("destroy_invalid_not_rejected");
  }
  /* Also: lookup(PID_INVALID) is PROC_ERR_INVALID_PID, not a crash. */
  if (process_lookup(PID_INVALID, &snap) != PROC_ERR_INVALID_PID) {
    die("lookup_invalid_pid_not_rejected");
  }
  printf("TEST:PASS:process_table_destroy_semantics\n");
}

/* Check 3: create→destroy→create on an empty table reuses the slot
 * index but advances the generation half. Mirrors the
 * #220/#237 generation-counter contract so the three subsystems
 * share one mental model. */
static void check_pid_advances_on_reuse(void) {
  reset_world();
  process_id_t a = create_or_die((cap_subject_id_t)1u, NULL, "first_create");
  if (process_destroy(a) != PROC_OK) {
    die("destroy_a");
  }
  process_id_t b = create_or_die((cap_subject_id_t)1u, NULL, "second_create");
  if (a == b) {
    die("pid_did_not_advance_after_destroy_create");
  }
  uint16_t a_idx = (uint16_t)(a & 0xFFFFu);
  uint16_t b_idx = (uint16_t)(b & 0xFFFFu);
  uint16_t a_gen = (uint16_t)((a >> 16) & 0xFFFFu);
  uint16_t b_gen = (uint16_t)((b >> 16) & 0xFFFFu);
  if (a_idx != b_idx) {
    die("reused_slot_index_changed");
  }
  if (b_gen == a_gen) {
    die("generation_did_not_advance");
  }
  if (process_destroy(b) != PROC_OK) {
    die("destroy_b");
  }
  printf("TEST:PASS:process_table_pid_advances\n");
}

/* Check 4: table-full rejection. Fill to PROC_TABLE_MAX, then one
 * more create must fail with PROC_ERR_TABLE_FULL and PID_INVALID. */
static void check_table_full_rejected(void) {
  reset_world();
  process_id_t pids[PROC_TABLE_MAX];
  for (uint32_t i = 0; i < PROC_TABLE_MAX; ++i) {
    pids[i] = create_or_die((cap_subject_id_t)(100u + i), NULL,
                            "table_full_fill");
  }
  /* All PIDs must be distinct. */
  for (uint32_t i = 0; i < PROC_TABLE_MAX; ++i) {
    for (uint32_t j = i + 1u; j < PROC_TABLE_MAX; ++j) {
      if (pids[i] == pids[j]) {
        die("duplicate_pid_in_full_table");
      }
    }
  }
  /* One more must be rejected. */
  process_id_t overflow_pid = (process_id_t)0xDEADBEEFu;
  proc_result_t rc = process_create((cap_subject_id_t)999u, NULL,
                                    &overflow_pid);
  if (rc != PROC_ERR_TABLE_FULL) {
    die("table_full_not_signalled");
  }
  if (overflow_pid != PID_INVALID) {
    die("table_full_did_not_clear_out_pid");
  }
  /* NULL out-param is PROC_ERR_INVALID_ARG regardless of table state. */
  if (process_create((cap_subject_id_t)1u, NULL, NULL) != PROC_ERR_INVALID_ARG) {
    die("create_null_outparam_not_rejected");
  }
  /* Freeing a slot must reopen exactly one create. */
  if (process_destroy(pids[3]) != PROC_OK) {
    die("free_one_slot_failed");
  }
  process_id_t reused = create_or_die((cap_subject_id_t)1234u, NULL,
                                      "reuse_after_free");
  /* Slot index should be the one we just freed. */
  if ((uint16_t)(reused & 0xFFFFu) != (uint16_t)(pids[3] & 0xFFFFu)) {
    die("freed_slot_not_reused_first");
  }
  printf("TEST:PASS:process_table_full_rejected\n");
}

/* Check 5: reset invalidates all previously issued PIDs. */
static void check_reset_invalidates(void) {
  reset_world();
  process_id_t pid = create_or_die((cap_subject_id_t)5u, NULL, "reset_create");
  if (!process_is_live_for_tests(pid)) {
    die("reset_precheck_live_false");
  }
  process_table_reset();
  if (process_is_live_for_tests(pid)) {
    die("reset_did_not_invalidate_live");
  }
  process_t snap;
  if (process_lookup(pid, &snap) != PROC_ERR_INVALID_PID) {
    die("reset_lookup_not_rejected");
  }
  /* After reset the next create on slot 0 must NOT collide with the
   * pre-reset PID's generation. */
  process_id_t fresh = create_or_die((cap_subject_id_t)5u, NULL,
                                     "reset_then_create");
  if (fresh == pid) {
    die("reset_did_not_advance_generation");
  }
  printf("TEST:PASS:process_table_reset_invalidates\n");
}

int main(void) {
  check_create_lookup_roundtrip();
  check_destroy_semantics();
  check_pid_advances_on_reuse();
  check_table_full_rejected();
  check_reset_invalidates();
  printf("TEST:PASS:process_table\n");
  return 0;
}
