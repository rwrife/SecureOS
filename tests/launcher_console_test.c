/**
 * @file launcher_console_test.c
 * @brief Tests for the launcher-mediated console-write capability slice.
 *
 * Covers issue #81 / plan 2026-04-11-console-launcher-capability-slice.md:
 *   - allow path: launcher grant + launcher write succeeds
 *   - deny path: app without launcher grant cannot write
 *   - regression: writes attempted without launcher mediation (direct
 *     gate call with no launcher grant) are denied by default
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/user/launcher.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_console:%s\n", reason);
  exit(1);
}

int main(void) {
  const cap_subject_id_t app_a = 2u;
  const cap_subject_id_t app_b = 3u;
  size_t bytes_written = 0u;

  printf("TEST:START:launcher_console\n");

  cap_table_init();
  launcher_reset();

  /* Deny-by-default: unregistered app cannot write through the launcher. */
  if (launcher_app_console_write(app_a, "hi", &bytes_written) != LAUNCHER_ERR_NOT_REGISTERED) {
    fail("unregistered_write_not_rejected");
  }

  if (launcher_register_app(app_a) != LAUNCHER_OK) {
    fail("register_app_a_failed");
  }
  if (launcher_register_app(app_b) != LAUNCHER_OK) {
    fail("register_app_b_failed");
  }

  /* Deny path: registered but not granted -> launcher denies and gate denies. */
  if (launcher_app_has_console_write(app_a) != 0) {
    fail("default_app_has_console_write");
  }
  if (launcher_app_console_write(app_a, "hi", &bytes_written) != LAUNCHER_ERR_DENIED) {
    fail("registered_no_grant_not_denied");
  }
  printf("TEST:PASS:launcher_console_deny_without_grant\n");

  /* Allow path: explicit launcher grant -> write succeeds with correct count. */
  if (launcher_grant_console_write(app_a) != LAUNCHER_OK) {
    fail("grant_console_write_failed");
  }
  if (launcher_app_has_console_write(app_a) != 1) {
    fail("after_grant_inspection_negative");
  }
  bytes_written = 0u;
  if (launcher_app_console_write(app_a, "secureos", &bytes_written) != LAUNCHER_OK) {
    fail("granted_write_denied");
  }
  if (bytes_written != 8u) {
    fail("granted_write_wrong_count");
  }
  printf("TEST:PASS:launcher_console_allow_after_grant\n");

  /* Regression: launcher grant for app_a must not leak to app_b. */
  if (launcher_app_has_console_write(app_b) != 0) {
    fail("grant_leaked_to_other_app");
  }
  if (launcher_app_console_write(app_b, "x", &bytes_written) != LAUNCHER_ERR_DENIED) {
    fail("grant_leaked_write_other_app");
  }

  /*
   * Regression: an app attempting to write to the console *without* going
   * through the launcher path must fail closed. The only way the capability
   * table holds CAP_CONSOLE_WRITE for an app subject is via the launcher,
   * so a direct gate call on a non-launcher subject is denied.
   */
  const cap_subject_id_t rogue_subject = 4u;
  if (cap_console_write_gate(rogue_subject, "bypass", &bytes_written) != CAP_ERR_MISSING) {
    fail("bypass_path_not_denied");
  }
  printf("TEST:PASS:launcher_console_regression_bypass_denied\n");

  /* Revoke restores deny-by-default. */
  if (launcher_revoke_console_write(app_a) != LAUNCHER_OK) {
    fail("revoke_console_write_failed");
  }
  if (launcher_app_has_console_write(app_a) != 0) {
    fail("after_revoke_still_has_console_write");
  }
  if (launcher_app_console_write(app_a, "hi", &bytes_written) != LAUNCHER_ERR_DENIED) {
    fail("after_revoke_write_not_denied");
  }
  printf("TEST:PASS:launcher_console_revoke_restores_deny\n");

  /* Invalid app handling. */
  if (launcher_register_app(CAP_TABLE_MAX_SUBJECTS) != LAUNCHER_ERR_INVALID_APP) {
    fail("invalid_app_register_not_rejected");
  }
  if (launcher_grant_console_write(CAP_TABLE_MAX_SUBJECTS) != LAUNCHER_ERR_INVALID_APP) {
    fail("invalid_app_grant_not_rejected");
  }
  if (launcher_app_console_write(CAP_TABLE_MAX_SUBJECTS, "x", &bytes_written)
      != LAUNCHER_ERR_INVALID_APP) {
    fail("invalid_app_write_not_rejected");
  }
  printf("TEST:PASS:launcher_console_invalid_app\n");

  /* Reset clears registrations and grants. */
  launcher_reset();
  if (launcher_app_has_console_write(app_a) != 0) {
    fail("reset_left_grant_intact");
  }
  if (launcher_app_console_write(app_a, "x", &bytes_written) != LAUNCHER_ERR_NOT_REGISTERED) {
    fail("reset_did_not_clear_registration");
  }
  printf("TEST:PASS:launcher_console_reset_clears_state\n");

  return 0;
}
