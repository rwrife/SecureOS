/**
 * @file broker_svc_delete_owner_authority_check_test.c
 * @brief Validator for `cap_broker_delete_owner_check` (M5-SUBSTRATE-002,
 *        issue #324).
 *
 * Pins the v0 authority policy documented in
 * `kernel/svc/broker_svc.h`:
 *
 *   - actor == owner            -> ALLOW (self-delete), no marker.
 *   - actor == SUBJECT_M5_ADMIN -> ALLOW (admin override stub), no marker.
 *   - everything else           -> DENY, with exactly one canonical
 *                                  CAP:DENY:<actor>:capability_admin:
 *                                  delete_owner_<owner_id> marker line
 *                                  emitted to stdout. The marker round-
 *                                  trips through the #221 conformance
 *                                  validator (cap_deny_marker_validate).
 *
 * Output markers (consumed by
 * build/scripts/test_broker_svc_delete_owner_authority_check.sh):
 *   TEST:PASS:broker_svc_delete_owner_authority_check_self_allow
 *   TEST:PASS:broker_svc_delete_owner_authority_check_admin_allow
 *   TEST:PASS:broker_svc_delete_owner_authority_check_bystander_deny
 *   TEST:PASS:broker_svc_delete_owner_authority_check_deny_marker_grammar
 *   TEST:PASS:broker_svc_delete_owner_authority_check
 *
 * Pure host-side, no kernel runtime dependencies. The deny-marker
 * emission is captured by redirecting stdout through a temp file so
 * the test can both assert exact-shape and feed the captured line
 * back through `cap_deny_marker_validate`.
 *
 * Issue: #324. Plan: plans/2026-05-25-m5-ownership-on-m1-substrate.md
 * slice 2.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/capability.h"
#include "../kernel/svc/broker_svc.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:broker_svc_delete_owner_authority_check:%s\n", reason);
  g_fail = 1;
}

/* Capture stdout for a single call to `body(arg)`, return the captured
 * bytes in `out_buf`. Returns the number of bytes captured. */
static size_t capture_stdout(int (*body)(cap_subject_id_t, cap_subject_id_t),
                             cap_subject_id_t actor,
                             cap_subject_id_t owner,
                             int *out_rc,
                             char *out_buf,
                             size_t out_buf_sz) {
  fflush(stdout);
  int saved_stdout = dup(STDOUT_FILENO);
  if (saved_stdout < 0) { fail("dup_stdout_failed"); return 0; }
  FILE *tmp = tmpfile();
  if (tmp == NULL) { close(saved_stdout); fail("tmpfile_failed"); return 0; }
  int tmp_fd = fileno(tmp);
  if (dup2(tmp_fd, STDOUT_FILENO) < 0) {
    fclose(tmp); close(saved_stdout); fail("dup2_failed"); return 0;
  }

  *out_rc = body(actor, owner);
  fflush(stdout);

  /* Restore stdout. */
  if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
    fclose(tmp); close(saved_stdout); fail("dup2_restore_failed"); return 0;
  }
  close(saved_stdout);

  /* Read captured bytes. */
  rewind(tmp);
  size_t n = fread(out_buf, 1u, out_buf_sz - 1u, tmp);
  out_buf[n] = '\0';
  fclose(tmp);
  return n;
}

static void test_self_allow(void) {
  char buf[512] = {0};
  int  rc = -1;
  size_t n = capture_stdout(cap_broker_delete_owner_check,
                            (cap_subject_id_t)42u,
                            (cap_subject_id_t)42u,
                            &rc, buf, sizeof(buf));
  if (rc != 1) { fail("self_not_allowed"); return; }
  if (n != 0u) {
    /* Self-allow MUST NOT emit a deny marker. */
    fail("self_emitted_marker");
    return;
  }
  printf("TEST:PASS:broker_svc_delete_owner_authority_check_self_allow\n");
}

static void test_admin_allow(void) {
  char buf[512] = {0};
  int  rc = -1;
  size_t n = capture_stdout(cap_broker_delete_owner_check,
                            (cap_subject_id_t)SUBJECT_M5_ADMIN,
                            (cap_subject_id_t)42u,
                            &rc, buf, sizeof(buf));
  if (rc != 1) { fail("admin_not_allowed"); return; }
  if (n != 0u) { fail("admin_emitted_marker"); return; }
  printf("TEST:PASS:broker_svc_delete_owner_authority_check_admin_allow\n");
}

static void test_bystander_deny(void) {
  char buf[512] = {0};
  int  rc = -1;
  size_t n = capture_stdout(cap_broker_delete_owner_check,
                            /*actor=*/(cap_subject_id_t)7u,
                            /*owner=*/(cap_subject_id_t)42u,
                            &rc, buf, sizeof(buf));
  if (rc != 0) { fail("bystander_not_denied"); return; }
  if (n == 0u) { fail("bystander_no_marker"); return; }

  /* Exact-shape: "CAP:DENY:7:capability_admin:delete_owner_42\n". */
  const char *expect = "CAP:DENY:7:capability_admin:delete_owner_42\n";
  if (strcmp(buf, expect) != 0) {
    fprintf(stderr, "got: [%s]\nexp: [%s]\n", buf, expect);
    fail("bystander_marker_wrong_shape");
    return;
  }
  printf("TEST:PASS:broker_svc_delete_owner_authority_check_bystander_deny\n");
}

static void test_deny_marker_grammar(void) {
  /* Feed several distinct deny invocations through the #221
   * conformance validator; every captured line MUST round-trip
   * cleanly. */
  const cap_subject_id_t actors[] = {3u, 9u, 1234u};
  const cap_subject_id_t owners[] = {1u, 42u, 65535u};
  for (size_t i = 0u; i < sizeof(actors) / sizeof(actors[0]); ++i) {
    char buf[512] = {0};
    int  rc = -1;
    size_t n = capture_stdout(cap_broker_delete_owner_check,
                              actors[i], owners[i],
                              &rc, buf, sizeof(buf));
    if (rc != 0 || n == 0u) {
      fail("grammar_setup_no_marker");
      return;
    }
    char reason[64] = {0};
    int  vr = cap_deny_marker_validate(buf, reason, sizeof(reason));
    if (vr != 0) {
      fprintf(stderr, "validate rc=%d reason=%s line=[%s]\n", vr, reason, buf);
      fail("grammar_validate_rejected");
      return;
    }
  }
  printf("TEST:PASS:broker_svc_delete_owner_authority_check_deny_marker_grammar\n");
}

int main(void) {
  test_self_allow();
  test_admin_allow();
  test_bystander_deny();
  test_deny_marker_grammar();

  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:broker_svc_delete_owner_authority_check\n");
  return 0;
}
