/**
 * @file helloapp_deny_test.c
 * @brief Deny-path acceptance test for HelloApp console-write.
 *
 * Purpose:
 *   Deterministically validates the deny path called out in
 *   BUILD_ROADMAP.md §5.2 (M2: Console service + Launcher + HelloApp):
 *     "deny path: HelloApp cannot print and receives defined
 *      error/fallback"
 *
 *   Simulates a HelloApp subject whose launcher manifest does *not*
 *   request CAP_CONSOLE_WRITE. The launcher actor (holding
 *   CAP_CAPABILITY_ADMIN) never grants the capability. The HelloApp
 *   write attempt through cap_console_write_gate must:
 *
 *     1. Return CAP_ERR_MISSING (the defined error/fallback).
 *     2. Leave the caller's "bytes_written" sentinel untouched, so no
 *        output is implicitly produced.
 *     3. Emit the structured pass marker
 *
 *           TEST:PASS:helloapp_denied_console_write
 *
 *   Additionally, we assert that the capability audit ring captured a
 *   deny event for the attempt (operation=CAP_AUDIT_OP_CHECK,
 *   capability=CAP_CONSOLE_WRITE, result=CAP_ERR_MISSING). This is the
 *   "capability audit deny event is recorded for the attempt" bullet in
 *   #92 and the structured deny-log hook tracked in #84.
 *
 * Regression coverage:
 *   - A non-launcher actor cannot grant CAP_CONSOLE_WRITE (mediation).
 *   - The launcher actor's existence does not implicitly widen
 *     HelloApp's permissions.
 *
 * Interactions:
 *   - kernel/cap/capability.c, cap_table.c, cap_gate.c
 *
 * Launched by:
 *   build/scripts/test_helloapp_deny.sh, dispatched via test.sh and
 *   validate_bundle.sh.
 *
 * Issue: #92.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"

#define HELLOAPP_BANNER "HelloApp: secureos M2 banner"
#define BYTES_WRITTEN_SENTINEL ((size_t)0xC0DECAFEu)

static const cap_subject_id_t LAUNCHER_ROOT_SUBJECT = 0u;
static const cap_subject_id_t LAUNCHER_SUBJECT = 1u;
static const cap_subject_id_t HELLOAPP_SUBJECT = 3u;
static const cap_subject_id_t BYSTANDER_SUBJECT = 5u;

static void die(const char *reason) {
  printf("TEST:FAIL:helloapp_deny:%s\n", reason);
  exit(1);
}

static int audit_ring_contains_deny(cap_subject_id_t subject_id,
                                    capability_id_t capability_id) {
  size_t count = cap_audit_count_for_tests();
  for (size_t i = 0u; i < count; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) {
      continue;
    }
    if (event.operation == CAP_AUDIT_OP_CHECK &&
        event.subject_id == subject_id &&
        event.capability_id == capability_id &&
        event.result == CAP_ERR_MISSING) {
      return 1;
    }
  }
  return 0;
}

int main(void) {
  printf("TEST:START:helloapp_deny\n");

  cap_reset_for_tests();

  /* Bootstrap a launcher actor with CAP_CAPABILITY_ADMIN. The launcher
   * exists, but for this scenario its manifest for HelloApp omits
   * CAP_CONSOLE_WRITE -- so it never issues the grant. */
  if (cap_grant_as_for_tests(LAUNCHER_ROOT_SUBJECT,
                             LAUNCHER_SUBJECT,
                             CAP_CAPABILITY_ADMIN) != CAP_OK) {
    die("bootstrap_launcher_admin");
  }

  /* HelloApp attempts to print without an explicit grant. The gate must
   * deny it and the bytes_written sentinel must remain untouched. */
  size_t bytes_written = BYTES_WRITTEN_SENTINEL;
  cap_result_t result =
      cap_console_write_gate(HELLOAPP_SUBJECT, HELLOAPP_BANNER, &bytes_written);
  if (result != CAP_ERR_MISSING) {
    die("expected_deny_missing");
  }
  if (bytes_written != BYTES_WRITTEN_SENTINEL) {
    die("deny_clobbered_bytes_written");
  }

  /* Audit deny event must be captured for the attempt. */
  if (!audit_ring_contains_deny(HELLOAPP_SUBJECT, CAP_CONSOLE_WRITE)) {
    die("missing_audit_deny_event");
  }

  /* Regression: a bystander without CAP_CAPABILITY_ADMIN cannot grant
   * console-write on HelloApp's behalf -- launcher mediation is the only
   * sanctioned path. */
  if (cap_grant_as_for_tests(BYSTANDER_SUBJECT,
                             HELLOAPP_SUBJECT,
                             CAP_CONSOLE_WRITE) == CAP_OK) {
    die("bystander_grant_unexpectedly_succeeded");
  }
  if (cap_console_write_gate(HELLOAPP_SUBJECT, HELLOAPP_BANNER, NULL) !=
      CAP_ERR_MISSING) {
    die("deny_after_bystander_grant_attempt");
  }

  /* Regression: the launcher actor having CAP_CAPABILITY_ADMIN does not
   * itself widen HelloApp's permissions -- a grant must be explicit. */
  if (cap_table_check(HELLOAPP_SUBJECT, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    die("launcher_admin_leaked_to_helloapp");
  }

  printf("TEST:PASS:helloapp_denied_console_write\n");
  printf("TEST:PASS:helloapp_deny\n");
  return 0;
}
