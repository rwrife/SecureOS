/**
 * @file helloapp_allow_test.c
 * @brief Allow-path acceptance test for HelloApp console-write.
 *
 * Purpose:
 *   Deterministically validates the allow path called out in
 *   BUILD_ROADMAP.md §5.2 (M2: Console service + Launcher + HelloApp):
 *     "grant path: HelloApp prints"
 *
 *   Simulates a HelloApp subject whose launcher manifest *does* request
 *   CAP_CONSOLE_WRITE. A launcher actor (holding CAP_CAPABILITY_ADMIN)
 *   grants the capability through cap_grant_as_for_tests, after which
 *   the HelloApp's banner write through cap_console_write_gate must
 *   succeed and the structured pass marker
 *
 *       TEST:PASS:helloapp_allowed_console_write
 *
 *   must be emitted on stdout. We additionally assert the capability
 *   audit ring captured both the launcher's GRANT and the HelloApp's
 *   CHECK with result CAP_OK -- this is the structured-audit hook
 *   tracked in issue #84; the same fields are already exposed via
 *   cap_audit_get_for_tests today so we exercise them now.
 *
 * Interactions:
 *   - kernel/cap/capability.c, cap_table.c, cap_gate.c
 *
 * Launched by:
 *   build/scripts/test_helloapp_allow.sh, dispatched via test.sh and
 *   validate_bundle.sh.
 *
 * Issue: #92.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"

#define HELLOAPP_BANNER "HelloApp: secureos M2 banner"

static const cap_subject_id_t LAUNCHER_ROOT_SUBJECT = 0u;
static const cap_subject_id_t LAUNCHER_SUBJECT = 1u;
static const cap_subject_id_t HELLOAPP_SUBJECT = 3u;

static void die(const char *reason) {
  printf("TEST:FAIL:helloapp_allow:%s\n", reason);
  exit(1);
}

/*
 * Simulated HelloApp "main": tries to write its banner through the
 * capability-gated console-write path. Returns the number of bytes the
 * gate said it would emit on success, or 0 when the gate denies.
 *
 * In a fully wired stack this would route through the launcher's
 * console service (kernel/user/launcher.c, landing alongside #82/#87).
 * The contract under test here is "deny-by-default unless the launcher
 * explicitly granted CAP_CONSOLE_WRITE", which is independent of which
 * file ultimately owns the launcher entrypoint.
 */
static size_t helloapp_emit_banner(cap_subject_id_t subject_id) {
  size_t bytes_written = 0u;
  cap_result_t result =
      cap_console_write_gate(subject_id, HELLOAPP_BANNER, &bytes_written);
  if (result != CAP_OK) {
    return 0u;
  }
  return bytes_written;
}

int main(void) {
  printf("TEST:START:helloapp_allow\n");

  cap_reset_for_tests();

  /* Bootstrap a launcher actor that holds CAP_CAPABILITY_ADMIN, mirroring
   * how the real launcher would be installed at boot. */
  if (cap_grant_as_for_tests(LAUNCHER_ROOT_SUBJECT,
                             LAUNCHER_SUBJECT,
                             CAP_CAPABILITY_ADMIN) != CAP_OK) {
    die("bootstrap_launcher_admin");
  }

  /* Launcher manifest for HelloApp requests CAP_CONSOLE_WRITE. The grant
   * goes through cap_grant_as_for_tests so it is auditable -- this is
   * the launcher-mediated grant the roadmap requires. */
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT,
                             HELLOAPP_SUBJECT,
                             CAP_CONSOLE_WRITE) != CAP_OK) {
    die("launcher_grant_console_write");
  }

  size_t bytes_written = helloapp_emit_banner(HELLOAPP_SUBJECT);
  if (bytes_written != strlen(HELLOAPP_BANNER)) {
    die("banner_byte_count_mismatch");
  }

  /* Audit-event assertions: the audit ring must hold, in order,
   *   [0] GRANT launcher CAP_CAPABILITY_ADMIN
   *   [1] GRANT HelloApp CAP_CONSOLE_WRITE (launcher-mediated)
   *   [2] CHECK HelloApp CAP_CONSOLE_WRITE result=CAP_OK
   *
   * Indexes match the cap_check audit record emitted from
   * cap_console_write_gate. This is the structured-record hook #84
   * tracks; the data is available on main today via the
   * cap_audit_*_for_tests API, so we exercise it now.
   */
  if (cap_audit_count_for_tests() < 3u) {
    die("audit_event_count_too_small");
  }

  cap_audit_event_t event = {0};
  if (cap_audit_get_for_tests(1u, &event) != CAP_OK) {
    die("audit_get_grant_event");
  }
  if (event.operation != CAP_AUDIT_OP_GRANT ||
      event.actor_subject_id != LAUNCHER_SUBJECT ||
      event.subject_id != HELLOAPP_SUBJECT ||
      event.capability_id != CAP_CONSOLE_WRITE ||
      event.result != CAP_OK) {
    die("audit_grant_event_fields");
  }

  if (cap_audit_get_for_tests(2u, &event) != CAP_OK) {
    die("audit_get_check_event");
  }
  if (event.operation != CAP_AUDIT_OP_CHECK ||
      event.subject_id != HELLOAPP_SUBJECT ||
      event.capability_id != CAP_CONSOLE_WRITE ||
      event.result != CAP_OK) {
    die("audit_check_event_fields");
  }

  printf("TEST:PASS:helloapp_allowed_console_write\n");
  printf("TEST:PASS:helloapp_allow\n");
  return 0;
}
