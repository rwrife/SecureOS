#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_audit:%s\n", reason);
  exit(1);
}

static void expect_event(size_t index,
                         cap_subject_id_t subject,
                         capability_id_t capability,
                         cap_result_t result,
                         const char *reason) {
  cap_audit_event_t event = {0};
  if (cap_audit_get_for_tests(index, &event) != CAP_OK) {
    fail(reason);
  }

  if (event.subject_id != subject || event.capability_id != capability || event.result != result) {
    fail(reason);
  }
}

int main(void) {
  printf("TEST:START:capability_audit\n");

  cap_reset_for_tests();

  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny_result");
  }
  expect_event(0u, 1u, CAP_CONSOLE_WRITE, CAP_ERR_MISSING, "default_deny_event");

  if (cap_grant_for_tests(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_failed");
  }
  if (cap_check(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("allow_result");
  }
  expect_event(1u, 1u, CAP_CONSOLE_WRITE, CAP_OK, "allow_event");

  if (cap_check(999u, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_result");
  }
  expect_event(2u, 999u, CAP_CONSOLE_WRITE, CAP_ERR_SUBJECT_INVALID, "invalid_subject_event");

  if (cap_check(1u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap_result");
  }
  expect_event(3u, 1u, (capability_id_t)999u, CAP_ERR_CAP_INVALID, "invalid_cap_event");
  printf("TEST:PASS:capability_audit_core_paths\n");

  cap_reset_for_tests();
  for (size_t i = 0; i < (size_t)CAP_AUDIT_EVENT_MAX + 5u; ++i) {
    (void)cap_check((cap_subject_id_t)i, CAP_CONSOLE_WRITE);
  }

  if (cap_audit_count_for_tests() != (size_t)CAP_AUDIT_EVENT_MAX) {
    fail("audit_ring_count");
  }

  expect_event(0u,
               5u,
               CAP_CONSOLE_WRITE,
               CAP_ERR_MISSING,
               "audit_ring_oldest_after_wrap");
  expect_event((size_t)CAP_AUDIT_EVENT_MAX - 1u,
               (cap_subject_id_t)(CAP_AUDIT_EVENT_MAX + 4u),
               CAP_CONSOLE_WRITE,
               CAP_ERR_SUBJECT_INVALID,
               "audit_ring_latest_after_wrap");

  if (cap_audit_get_for_tests((size_t)CAP_AUDIT_EVENT_MAX, 0) != CAP_ERR_CAP_INVALID) {
    fail("audit_out_of_bounds_rejected");
  }
  printf("TEST:PASS:capability_audit_ring_wrap\n");

  return 0;
}
