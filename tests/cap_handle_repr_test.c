/**
 * @file cap_handle_repr_test.c
 * @brief Unit tests for the M1-CAPTBL-002 32-bit capability handle layer
 *        (issue #233, plan #197).
 *
 * Purpose:
 *   Exercises the wire-stable cap_handle_t representation, the
 *   cap_handle_grant -> cap_gate_check_handle -> cap_handle_revoke cycle,
 *   and the ABI-freeze invariants pinned against OS_ABI_VERSION=0 (#150).
 *
 *   Cases:
 *     1. Layout freeze: bit widths add to 32, tag/slot/gen pack+unpack
 *        round-trip, and the runtime ABI macro matches the source header.
 *     2. Grant -> check passes; revoke -> same numeric handle now denies
 *        with CAP_ERR_MISSING (plan acceptance #4: the "generation check"
 *        test).
 *     3. Malformed handles (wrong tag, slot out of bounds, null) are
 *        rejected with CAP_ERR_CAP_INVALID and never accepted by
 *        cap_gate_check_handle.
 *     4. Wrong-cap handle: a handle granted for CAP_CONSOLE_WRITE does NOT
 *        authorize CAP_SERIAL_WRITE on the same row.
 *     5. Idempotent grant returns the SAME handle for a live (subject, cap).
 *     6. Repeated grant-then-revoke bumps the generation each time and the
 *        successive handles are distinct numeric values.
 *
 * Interactions:
 *   - cap_handle.{c,h}: the unit under test.
 *   - capability.h: shared cap_result_t / capability_id_t vocabulary.
 *   - user/include/secureos_abi.h: source of truth for OS_ABI_VERSION.
 *
 * Launched by:
 *   build/scripts/test_cap_handle_repr.sh (registered with
 *   build/scripts/test.sh under the `cap_handle_repr` target).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/capability.h"
#include "../user/include/secureos_abi.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_handle_repr:%s\n", reason);
  exit(1);
}

static void test_layout_freeze(void) {
  /* Bit-width sum is enforced at compile time via #error in cap_handle.h.
   * Here we assert the runtime helpers respect the same layout. */
  if (CAP_HANDLE_SLOT_BITS + CAP_HANDLE_GEN_BITS + CAP_HANDLE_TAG_BITS != 32u) {
    fail("layout_bit_sum");
  }
  if (CAP_HANDLE_TAG_KERNEL != 0x1u) {
    fail("layout_tag_kernel_value");
  }

  /* Round-trip through pack/unpack. */
  cap_handle_t h = cap_handle_pack((uint16_t)0xABCDu, (uint16_t)0x1FFFu,
                                    (uint8_t)CAP_HANDLE_TAG_KERNEL);
  if (cap_handle_slot(h) != (uint16_t)0xABCDu) {
    fail("pack_slot_roundtrip");
  }
  if (cap_handle_generation(h) != (uint16_t)0x1FFFu) {
    fail("pack_gen_roundtrip");
  }
  if (cap_handle_tag(h) != (uint8_t)CAP_HANDLE_TAG_KERNEL) {
    fail("pack_tag_roundtrip");
  }

  /* CAP_HANDLE_NULL must have tag=0 and so must NEVER validate as kernel. */
  if (cap_handle_tag(CAP_HANDLE_NULL) == CAP_HANDLE_TAG_KERNEL) {
    fail("null_handle_carries_kernel_tag");
  }

  /* ABI anchor: this slice is frozen at OS_ABI_VERSION=0. The build will
   * already #error on bump, but we double-check the runtime value matches. */
  if (OS_ABI_VERSION != 0) {
    fail("abi_version_not_zero");
  }
}

static void test_grant_check_revoke_cycle(void) {
  cap_handle_table_reset();

  cap_handle_t h = cap_handle_grant(2u, CAP_CONSOLE_WRITE);
  if (h == CAP_HANDLE_NULL) {
    fail("grant_returned_null");
  }
  if (cap_handle_tag(h) != CAP_HANDLE_TAG_KERNEL) {
    fail("grant_handle_tag");
  }
  if (cap_gate_check_handle(h, CAP_CONSOLE_WRITE) != 1) {
    fail("check_after_grant");
  }
  if (cap_gate_check_handle_result(h, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("check_result_after_grant");
  }

  /* Revoke: same numeric handle MUST now deny with CAP_ERR_MISSING. */
  if (cap_handle_revoke(h) != CAP_OK) {
    fail("revoke_failed");
  }
  if (cap_gate_check_handle(h, CAP_CONSOLE_WRITE) != 0) {
    fail("check_after_revoke_should_fail");
  }
  if (cap_gate_check_handle_result(h, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("check_result_after_revoke");
  }

  /* Double-revoke of the now-stale handle is a no-op. */
  cap_result_t r2 = cap_handle_revoke(h);
  if (r2 != CAP_ERR_MISSING) {
    fail("double_revoke_should_be_missing");
  }
}

static void test_malformed_handles_rejected(void) {
  cap_handle_table_reset();

  /* Null handle: tag bits 0, rejected as CAP_ERR_CAP_INVALID. */
  if (cap_gate_check_handle(CAP_HANDLE_NULL, CAP_CONSOLE_WRITE) != 0) {
    fail("null_handle_accepted");
  }
  if (cap_gate_check_handle_result(CAP_HANDLE_NULL, CAP_CONSOLE_WRITE) !=
      CAP_ERR_CAP_INVALID) {
    fail("null_handle_wrong_result");
  }

  /* Wrong tag bits (0b10) — reserved for future kinds, must deny today. */
  cap_handle_t bad_tag = cap_handle_pack(0u, 1u, 0x2u);
  if (cap_gate_check_handle_result(bad_tag, CAP_CONSOLE_WRITE) !=
      CAP_ERR_CAP_INVALID) {
    fail("bad_tag_not_rejected");
  }

  /* Slot index inside cap_handle_t's 16-bit field but beyond the table:
   * pick slot = CAP_HANDLE_TABLE_MAX. */
  cap_handle_t oob = cap_handle_pack((uint16_t)CAP_HANDLE_TABLE_MAX, 1u,
                                      (uint8_t)CAP_HANDLE_TAG_KERNEL);
  if (cap_gate_check_handle_result(oob, CAP_CONSOLE_WRITE) !=
      CAP_ERR_CAP_INVALID) {
    fail("oob_slot_not_rejected");
  }

  /* Revoke of a malformed handle returns the same error class. */
  if (cap_handle_revoke(CAP_HANDLE_NULL) != CAP_ERR_CAP_INVALID) {
    fail("revoke_null_wrong_result");
  }
  if (cap_handle_revoke(bad_tag) != CAP_ERR_CAP_INVALID) {
    fail("revoke_bad_tag_wrong_result");
  }
  if (cap_handle_revoke(oob) != CAP_ERR_CAP_INVALID) {
    fail("revoke_oob_wrong_result");
  }
}

static void test_wrong_cap_id_denied(void) {
  cap_handle_table_reset();

  cap_handle_t h = cap_handle_grant(3u, CAP_CONSOLE_WRITE);
  if (h == CAP_HANDLE_NULL) {
    fail("wrong_cap_grant_failed");
  }
  /* Handle authorizes CONSOLE_WRITE; checking it for SERIAL_WRITE must
   * fail with CAP_ERR_CAP_INVALID (capability mismatch, not stale). */
  if (cap_gate_check_handle_result(h, CAP_SERIAL_WRITE) !=
      CAP_ERR_CAP_INVALID) {
    fail("wrong_cap_should_deny");
  }
  if (cap_gate_check_handle(h, CAP_CONSOLE_WRITE) != 1) {
    fail("right_cap_should_pass");
  }
}

static void test_grant_idempotent_returns_same_handle(void) {
  cap_handle_table_reset();

  cap_handle_t h1 = cap_handle_grant(4u, CAP_FS_READ);
  if (h1 == CAP_HANDLE_NULL) {
    fail("idem_first_grant_null");
  }
  cap_handle_t h2 = cap_handle_grant(4u, CAP_FS_READ);
  if (h2 != h1) {
    fail("idem_grant_returned_different_handle");
  }
  if (cap_handle_table_live_count() != 1u) {
    fail("idem_grant_allocated_extra_row");
  }
  if (cap_gate_check_handle(h1, CAP_FS_READ) != 1) {
    fail("idem_check_h1");
  }
  if (cap_gate_check_handle(h2, CAP_FS_READ) != 1) {
    fail("idem_check_h2");
  }
}

static void test_generation_bump_distinct_handles(void) {
  cap_handle_table_reset();

  cap_handle_t h1 = cap_handle_grant(5u, CAP_DEBUG_EXIT);
  if (h1 == CAP_HANDLE_NULL) {
    fail("gen_g1_null");
  }
  if (cap_handle_revoke(h1) != CAP_OK) {
    fail("gen_r1_failed");
  }
  /* Second grant should reuse the same slot but a new generation, yielding
   * a numerically distinct handle. */
  cap_handle_t h2 = cap_handle_grant(5u, CAP_DEBUG_EXIT);
  if (h2 == CAP_HANDLE_NULL) {
    fail("gen_g2_null");
  }
  if (h1 == h2) {
    fail("gen_handles_collide_after_revoke");
  }
  /* The fresh row may land in a new slot or reuse the revoked one; the
   * plan only requires that the OLD handle stay stale. */
  if (cap_gate_check_handle(h1, CAP_DEBUG_EXIT) != 0) {
    fail("gen_h1_still_valid_after_revoke_and_regrant");
  }
  if (cap_gate_check_handle(h2, CAP_DEBUG_EXIT) != 1) {
    fail("gen_h2_should_be_valid");
  }
}

int main(void) {
  printf("TEST:START:cap_handle_repr\n");
  test_layout_freeze();
  test_grant_check_revoke_cycle();
  test_malformed_handles_rejected();
  test_wrong_cap_id_denied();
  test_grant_idempotent_returns_same_handle();
  test_generation_bump_distinct_handles();
  printf("TEST:PASS:cap_handle_repr\n");
  return 0;
}
