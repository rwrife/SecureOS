/**
 * @file cap_handle_revoke_subtree_test.c
 * @brief Unit tests for the M1-CAPTBL-004 reserved-symbol stub of
 *        cap_handle_revoke_subtree (issue #241, plan
 *        plans/2026-05-20-m1-kernel-capability-table.md).
 *
 * The v0 contract for cap_handle_revoke_subtree is "unconditionally
 * returns CAP_ERR_CAP_INVALID with zero side effects". These tests
 * prove exactly that:
 *
 *   1. Returns CAP_ERR_CAP_INVALID for CAP_HANDLE_NULL.
 *   2. Returns CAP_ERR_CAP_INVALID for an otherwise-valid live handle.
 *   3. After (2), the underlying row is still LIVE and
 *      cap_gate_check_handle still succeeds (proves the stub did not
 *      accidentally revoke or otherwise mutate the table).
 *   4. Returns CAP_ERR_CAP_INVALID for a malformed-tag handle, same as
 *      (1) \u2014 pins that the universal-failure contract is not
 *      accidentally tag-gated.
 *
 * Launched by:
 *   build/scripts/test_cap_handle_revoke_subtree.sh (registered with
 *   build/scripts/test.sh under the `cap_handle_revoke_subtree` target).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_handle_revoke_subtree:%s\n", reason);
  exit(1);
}

static void test_null_handle_invalid(void) {
  cap_handle_table_reset();
  if (cap_handle_revoke_subtree(CAP_HANDLE_NULL) != CAP_ERR_CAP_INVALID) {
    fail("CAP_HANDLE_NULL did not return CAP_ERR_CAP_INVALID");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_null_handle_invalid\n");
}

static void test_live_handle_invalid_no_side_effects(void) {
  cap_handle_table_reset();

  cap_handle_t h = cap_handle_grant((cap_subject_id_t)1, (capability_id_t)1);
  if (h == CAP_HANDLE_NULL) {
    fail("setup: grant returned CAP_HANDLE_NULL");
  }
  if (!cap_gate_check_handle(h, (capability_id_t)1)) {
    fail("setup: freshly granted handle did not pass gate");
  }
  uint32_t live_before = cap_handle_table_live_count();
  if (live_before != 1u) {
    fail("setup: expected live_count == 1 after grant");
  }

  if (cap_handle_revoke_subtree(h) != CAP_ERR_CAP_INVALID) {
    fail("live handle did not return CAP_ERR_CAP_INVALID");
  }

  /* No side effects: row is still LIVE, gate still passes, live_count
   * unchanged. */
  if (!cap_gate_check_handle(h, (capability_id_t)1)) {
    fail("handle was unexpectedly stale after stub call");
  }
  if (cap_handle_table_live_count() != live_before) {
    fail("live_count changed across stub call");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_no_side_effects\n");
}

static void test_malformed_tag_invalid(void) {
  cap_handle_table_reset();
  /* Tag 0b11 is reserved-and-invalid in v0. */
  cap_handle_t bad = cap_handle_pack(/*slot=*/0u, /*generation_low14=*/0u,
                                     /*tag=*/0x3u);
  if (cap_handle_revoke_subtree(bad) != CAP_ERR_CAP_INVALID) {
    fail("malformed-tag handle did not return CAP_ERR_CAP_INVALID");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_malformed_tag_invalid\n");
}

int main(void) {
  test_null_handle_invalid();
  test_live_handle_invalid_no_side_effects();
  test_malformed_tag_invalid();
  printf("TEST:PASS:cap_handle_revoke_subtree\n");
  return 0;
}
