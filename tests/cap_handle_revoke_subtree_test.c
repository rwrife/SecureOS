/**
 * @file cap_handle_revoke_subtree_test.c
 * @brief Unit tests for cap_handle_revoke_subtree.
 *
 * Originally landed (M1-CAPTBL-004, issue #241) as the reserved-symbol
 * stub that unconditionally returned CAP_ERR_CAP_INVALID. Re-targeted in
 * M5-SUBSTRATE-001 (issue #323, plan #319) when the BFS walker became
 * load-bearing for §5.5 ownership-graph cascading deletion.
 *
 * Contract under test:
 *
 *   1. CAP_HANDLE_NULL              -> CAP_ERR_CAP_INVALID (no-op).
 *   2. Malformed-tag handle         -> CAP_ERR_CAP_INVALID (no-op).
 *   3. Stale handle (post-revoke)   -> CAP_ERR_MISSING (no-op).
 *   4. Live root with descendants   -> CAP_OK; root + every transitive
 *      child via parent_handle is revoked; cap_gate_check_handle on
 *      each previously-issued handle now returns CAP_ERR_MISSING.
 *   5. Rows with parent_handle == CAP_HANDLE_NULL are NOT swept when an
 *      unrelated root drives the cascade (sentinel-no-op pin).
 *
 * Launched by:
 *   build/scripts/test_cap_handle_revoke_subtree.sh.
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

static void test_subtree_walks_chain(void) {
  cap_handle_table_reset();

  /* Build a 3-level chain: root -> child -> grandchild, all under
   * different (subject, cap_id) pairs so each gets its own row. */
  cap_handle_t root = cap_handle_grant_child(
      (cap_subject_id_t)1, (capability_id_t)1, CAP_HANDLE_NULL);
  if (root == CAP_HANDLE_NULL) {
    fail("setup: root grant returned NULL");
  }
  cap_handle_t child = cap_handle_grant_child(
      (cap_subject_id_t)2, (capability_id_t)1, root);
  if (child == CAP_HANDLE_NULL) {
    fail("setup: child grant returned NULL");
  }
  cap_handle_t grandchild = cap_handle_grant_child(
      (cap_subject_id_t)3, (capability_id_t)1, child);
  if (grandchild == CAP_HANDLE_NULL) {
    fail("setup: grandchild grant returned NULL");
  }

  /* All three handles gate-pass before the cascade. */
  if (!cap_gate_check_handle(root, (capability_id_t)1)) {
    fail("setup: root did not pass gate");
  }
  if (!cap_gate_check_handle(child, (capability_id_t)1)) {
    fail("setup: child did not pass gate");
  }
  if (!cap_gate_check_handle(grandchild, (capability_id_t)1)) {
    fail("setup: grandchild did not pass gate");
  }

  if (cap_handle_revoke_subtree(root) != CAP_OK) {
    fail("subtree-revoke on live root did not return CAP_OK");
  }

  /* All three handles must now deny via the staleness check. */
  if (cap_gate_check_handle(root, (capability_id_t)1)) {
    fail("root still passes gate after cascade");
  }
  if (cap_gate_check_handle(child, (capability_id_t)1)) {
    fail("child still passes gate after cascade");
  }
  if (cap_gate_check_handle(grandchild, (capability_id_t)1)) {
    fail("grandchild still passes gate after cascade");
  }
  if (cap_gate_check_handle_result(grandchild, (capability_id_t)1) !=
      CAP_ERR_MISSING) {
    fail("grandchild deny code != CAP_ERR_MISSING");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_walks_chain\n");
}

static void test_null_parent_no_op(void) {
  cap_handle_table_reset();

  /* Two unrelated sentinel-rooted handles. Cascading on one must not
   * touch the other, even though both have parent_handle == 0. */
  cap_handle_t a = cap_handle_grant_child(
      (cap_subject_id_t)1, (capability_id_t)1, CAP_HANDLE_NULL);
  cap_handle_t b = cap_handle_grant_child(
      (cap_subject_id_t)2, (capability_id_t)1, CAP_HANDLE_NULL);
  if (a == CAP_HANDLE_NULL || b == CAP_HANDLE_NULL) {
    fail("setup: sibling-root grants returned NULL");
  }

  if (cap_handle_revoke_subtree(a) != CAP_OK) {
    fail("subtree-revoke on root a did not return CAP_OK");
  }
  if (cap_gate_check_handle(a, (capability_id_t)1)) {
    fail("a still passes gate after self-cascade");
  }
  if (!cap_gate_check_handle(b, (capability_id_t)1)) {
    fail("unrelated sibling b incorrectly revoked by a's cascade");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_null_parent_no_op\n");
}

static void test_stale_root_missing(void) {
  cap_handle_table_reset();
  cap_handle_t h = cap_handle_grant_child(
      (cap_subject_id_t)1, (capability_id_t)1, CAP_HANDLE_NULL);
  if (h == CAP_HANDLE_NULL) {
    fail("setup: grant returned NULL");
  }
  if (cap_handle_revoke(h) != CAP_OK) {
    fail("setup: cap_handle_revoke != CAP_OK");
  }
  /* Same numeric handle, but the row's generation has moved on. */
  if (cap_handle_revoke_subtree(h) != CAP_ERR_MISSING) {
    fail("stale root did not return CAP_ERR_MISSING");
  }
  printf("TEST:PASS:cap_handle_revoke_subtree_stale_root_missing\n");
}

static void test_grant_forwarder_legacy_callers_unaffected(void) {
  cap_handle_table_reset();
  /* Legacy cap_handle_grant must still mint rows with parent==NULL so
   * unrelated cascades cannot sweep them transitively. */
  cap_handle_t legacy = cap_handle_grant((cap_subject_id_t)1,
                                          (capability_id_t)1);
  cap_handle_t other_root = cap_handle_grant_child(
      (cap_subject_id_t)2, (capability_id_t)1, CAP_HANDLE_NULL);
  if (legacy == CAP_HANDLE_NULL || other_root == CAP_HANDLE_NULL) {
    fail("setup: grants returned NULL");
  }
  if (cap_handle_revoke_subtree(other_root) != CAP_OK) {
    fail("subtree-revoke on other_root != CAP_OK");
  }
  if (!cap_gate_check_handle(legacy, (capability_id_t)1)) {
    fail("legacy-granted row swept by unrelated cascade");
  }
  printf("TEST:PASS:cap_handle_grant_forwarder_legacy_callers_unaffected\n");
}

int main(void) {
  test_null_handle_invalid();
  test_malformed_tag_invalid();
  test_subtree_walks_chain();
  test_null_parent_no_op();
  test_stale_root_missing();
  test_grant_forwarder_legacy_callers_unaffected();
  printf("TEST:PASS:cap_handle_revoke_subtree\n");
  return 0;
}
