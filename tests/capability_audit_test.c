#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_audit:%s\n", reason);
  exit(1);
}

static void expect_event(size_t index,
                         uint64_t sequence_id,
                         cap_audit_op_t operation,
                         cap_subject_id_t actor,
                         cap_subject_id_t subject,
                         capability_id_t capability,
                         cap_result_t result,
                         const char *reason) {
  cap_audit_event_t event = {0};
  if (cap_audit_get_for_tests(index, &event) != CAP_OK) {
    fail(reason);
  }

  if (event.sequence_id != sequence_id || event.operation != operation ||
      event.actor_subject_id != actor || event.subject_id != subject ||
      event.capability_id != capability || event.result != result) {
    fail(reason);
  }
}

int main(void) {
  printf("TEST:START:capability_audit\n");

  cap_reset_for_tests();

  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny_result");
  }
  expect_event(0u,
               0u,
               CAP_AUDIT_OP_CHECK,
               1u,
               1u,
               CAP_CONSOLE_WRITE,
               CAP_ERR_MISSING,
               "default_deny_event");

  if (cap_grant_for_tests(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_failed");
  }
  expect_event(1u,
               1u,
               CAP_AUDIT_OP_GRANT,
               1u,
               1u,
               CAP_CONSOLE_WRITE,
               CAP_OK,
               "grant_event");

  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("allow_result");
  }
  expect_event(2u, 2u, CAP_AUDIT_OP_CHECK, 1u, 1u, CAP_CONSOLE_WRITE, CAP_OK, "allow_event");

  if (cap_revoke_for_tests(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_failed");
  }
  expect_event(3u,
               3u,
               CAP_AUDIT_OP_REVOKE,
               1u,
               1u,
               CAP_CONSOLE_WRITE,
               CAP_OK,
               "revoke_event");

  if (cap_check(999u, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_result");
  }
  expect_event(4u,
               4u,
               CAP_AUDIT_OP_CHECK,
               999u,
               999u,
               CAP_CONSOLE_WRITE,
               CAP_ERR_SUBJECT_INVALID,
               "invalid_subject_event");

  if (cap_check(1u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap_result");
  }
  expect_event(5u,
               5u,
               CAP_AUDIT_OP_CHECK,
               1u,
               1u,
               (capability_id_t)999u,
               CAP_ERR_CAP_INVALID,
               "invalid_cap_event");
  printf("TEST:PASS:capability_audit_core_paths\n");

  cap_reset_for_tests();
  if (cap_grant_as_for_tests(0u, 2u, CAP_CAPABILITY_ADMIN) != CAP_OK) {
    fail("grant_as_bootstrap_root_admin_grant");
  }
  if (cap_grant_as_for_tests(2u, 3u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("grant_as_delegated_admin_serial_grant");
  }
  if (cap_revoke_as_for_tests(2u, 3u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("revoke_as_delegated_admin_serial_revoke");
  }
  if (cap_grant_as_for_tests(2u, 4u, CAP_CAPABILITY_ADMIN) != CAP_ERR_MISSING) {
    fail("grant_as_non_root_admin_grant_denied");
  }

  expect_event(0u,
               0u,
               CAP_AUDIT_OP_GRANT,
               0u,
               2u,
               CAP_CAPABILITY_ADMIN,
               CAP_OK,
               "grant_as_root_actor_attribution");
  expect_event(1u,
               1u,
               CAP_AUDIT_OP_GRANT,
               2u,
               3u,
               CAP_SERIAL_WRITE,
               CAP_OK,
               "grant_as_actor_attribution");
  expect_event(2u,
               2u,
               CAP_AUDIT_OP_REVOKE,
               2u,
               3u,
               CAP_SERIAL_WRITE,
               CAP_OK,
               "revoke_as_actor_attribution");
  expect_event(3u,
               3u,
               CAP_AUDIT_OP_GRANT,
               2u,
               4u,
               CAP_CAPABILITY_ADMIN,
               CAP_ERR_MISSING,
               "grant_as_denied_actor_attribution");
  printf("TEST:PASS:capability_audit_actor_attribution\n");

  cap_reset_for_tests();
  for (size_t i = 0; i < (size_t)CAP_AUDIT_EVENT_MAX + 5u; ++i) {
    (void)cap_check((cap_subject_id_t)i, CAP_CONSOLE_WRITE);
  }

  if (cap_audit_count_for_tests() != (size_t)CAP_AUDIT_EVENT_MAX) {
    fail("audit_ring_count");
  }
  if (cap_audit_dropped_for_tests() != 5u) {
    fail("audit_ring_dropped_count");
  }

  expect_event(0u,
               5u,
               CAP_AUDIT_OP_CHECK,
               5u,
               5u,
               CAP_CONSOLE_WRITE,
               CAP_ERR_MISSING,
               "audit_ring_oldest_after_wrap");
  expect_event((size_t)CAP_AUDIT_EVENT_MAX - 1u,
               (uint64_t)CAP_AUDIT_EVENT_MAX + 4u,
               CAP_AUDIT_OP_CHECK,
               (cap_subject_id_t)(CAP_AUDIT_EVENT_MAX + 4u),
               (cap_subject_id_t)(CAP_AUDIT_EVENT_MAX + 4u),
               CAP_CONSOLE_WRITE,
               CAP_ERR_SUBJECT_INVALID,
               "audit_ring_latest_after_wrap");

  if (cap_audit_get_for_tests((size_t)CAP_AUDIT_EVENT_MAX, 0) != CAP_ERR_CAP_INVALID) {
    fail("audit_out_of_bounds_rejected");
  }
  printf("TEST:PASS:capability_audit_ring_wrap\n");

  cap_reset_for_tests();

  for (size_t i = 0; i < 12u; ++i) {
    if (cap_grant_for_tests((cap_subject_id_t)i, CAP_SERIAL_WRITE) != CAP_OK && i < 8u) {
      fail("mixed_grant_result");
    }
    (void)cap_check((cap_subject_id_t)i, CAP_SERIAL_WRITE);
    (void)cap_revoke_for_tests((cap_subject_id_t)i, CAP_SERIAL_WRITE);
  }

  if (cap_audit_count_for_tests() != (size_t)CAP_AUDIT_EVENT_MAX) {
    fail("mixed_ring_count");
  }
  if (cap_audit_dropped_for_tests() != 4u) {
    fail("mixed_ring_dropped_count");
  }

  expect_event(0u,
               4u,
               CAP_AUDIT_OP_CHECK,
               1u,
               1u,
               CAP_SERIAL_WRITE,
               CAP_OK,
               "mixed_oldest_expected");
  expect_event((size_t)CAP_AUDIT_EVENT_MAX - 1u,
               35u,
               CAP_AUDIT_OP_REVOKE,
               11u,
               11u,
               CAP_SERIAL_WRITE,
               CAP_ERR_SUBJECT_INVALID,
               "mixed_latest_expected");

  printf("TEST:PASS:capability_audit_mixed_overflow\n");

  cap_reset_for_tests();
  for (size_t i = 0; i < 20u; ++i) {
    (void)cap_check((cap_subject_id_t)(i % 8u), CAP_CONSOLE_WRITE);
  }

  if (cap_audit_checkpoint_count_for_tests() != 2u) {
    fail("checkpoint_count_expected_two");
  }

  cap_audit_checkpoint_t checkpoint0 = {0};
  cap_audit_checkpoint_t checkpoint1 = {0};
  if (cap_audit_checkpoint_get_for_tests(0u, &checkpoint0) != CAP_OK) {
    fail("checkpoint0_read");
  }
  if (cap_audit_checkpoint_get_for_tests(1u, &checkpoint1) != CAP_OK) {
    fail("checkpoint1_read");
  }

  if (checkpoint0.checkpoint_id != 0u || checkpoint0.start_sequence_id != 0u ||
      checkpoint0.end_sequence_id != 7u || checkpoint0.dropped_count != 0u) {
    fail("checkpoint0_metadata");
  }
  if (checkpoint1.checkpoint_id != 1u || checkpoint1.start_sequence_id != 8u ||
      checkpoint1.end_sequence_id != 15u || checkpoint1.dropped_count != 0u) {
    fail("checkpoint1_metadata");
  }
  if (checkpoint0.seal == checkpoint1.seal) {
    fail("checkpoint_seal_progression");
  }

  cap_reset_for_tests();
  for (size_t i = 0; i < (size_t)CAP_AUDIT_EVENT_MAX + CAP_AUDIT_CHECKPOINT_INTERVAL; ++i) {
    (void)cap_check((cap_subject_id_t)i, CAP_CONSOLE_WRITE);
  }

  if (cap_audit_checkpoint_count_for_tests() == 0u) {
    fail("checkpoint_count_wrap_nonzero");
  }

  cap_audit_checkpoint_t checkpoint_last = {0};
  if (cap_audit_checkpoint_get_for_tests(cap_audit_checkpoint_count_for_tests() - 1u,
                                         &checkpoint_last) != CAP_OK) {
    fail("checkpoint_last_read");
  }

  if (checkpoint_last.dropped_count == 0u) {
    fail("checkpoint_dropped_count_reflects_wrap");
  }

  printf("TEST:PASS:capability_audit_checkpoints\n");

  size_t retained_count = cap_audit_count_for_tests();
  size_t dropped_count = cap_audit_dropped_for_tests();
  uint64_t first_sequence_id = 0u;
  uint64_t last_sequence_id = 0u;
  if (retained_count > 0u) {
    cap_audit_event_t first_event = {0};
    cap_audit_event_t last_event = {0};
    if (cap_audit_get_for_tests(0u, &first_event) != CAP_OK) {
      fail("audit_first_event_summary_read");
    }
    if (cap_audit_get_for_tests(retained_count - 1u, &last_event) != CAP_OK) {
      fail("audit_last_event_summary_read");
    }
    first_sequence_id = first_event.sequence_id;
    last_sequence_id = last_event.sequence_id;
  }

  size_t checkpoint_count = cap_audit_checkpoint_count_for_tests();
  uint64_t first_checkpoint_id = 0u;
  uint64_t latest_checkpoint_id = 0u;
  uint64_t latest_checkpoint_seal = 0u;
  size_t latest_checkpoint_dropped = 0u;
  if (checkpoint_count > 0u) {
    cap_audit_checkpoint_t first_checkpoint = {0};
    cap_audit_checkpoint_t latest_checkpoint = {0};
    if (cap_audit_checkpoint_get_for_tests(0u, &first_checkpoint) != CAP_OK) {
      fail("checkpoint_first_summary_read");
    }
    if (cap_audit_checkpoint_get_for_tests(checkpoint_count - 1u, &latest_checkpoint) != CAP_OK) {
      fail("checkpoint_latest_summary_read");
    }
    first_checkpoint_id = first_checkpoint.checkpoint_id;
    latest_checkpoint_id = latest_checkpoint.checkpoint_id;
    latest_checkpoint_seal = latest_checkpoint.seal;
    latest_checkpoint_dropped = latest_checkpoint.dropped_count;
  }

  printf("TEST:AUDIT_CHECKPOINT_SUMMARY:count=%zu:first_id=%llu:latest_id=%llu:latest_seal=%llu:latest_dropped=%zu\n",
         checkpoint_count,
         (unsigned long long)first_checkpoint_id,
         (unsigned long long)latest_checkpoint_id,
         (unsigned long long)latest_checkpoint_seal,
         latest_checkpoint_dropped);

  printf("TEST:AUDIT_SUMMARY:count=%zu:dropped=%zu:first_seq=%llu:last_seq=%llu:coverage=%s\n",
         retained_count,
         dropped_count,
         (unsigned long long)first_sequence_id,
         (unsigned long long)last_sequence_id,
         dropped_count > 0u ? "truncated" : "full");

  return 0;
}
