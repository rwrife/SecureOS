/**
 * @file process_create_table_full_deny_marker_test.c
 * @brief Asserts that process_create(), when the PROC_TABLE_MAX slot
 *        cap is reached, emits the canonical capability-denied marker
 *        defined by docs/abi/capability-deny-contract.md §4 (issue #261).
 *
 * Design recap (the long version lives in process.c next to
 * proc_emit_table_full_deny_marker):
 *
 *   PROC_TABLE_FULL is a resource-exhaustion deny, not a cap_check()
 *   deny, but #261 explicitly asks for the same `CAP:DENY:<...>` shape
 *   so cross-cutting greps in the launcher / acceptance suites pick it
 *   up alongside policy denies. The cap_deny_marker grammar locked by
 *   #211 / #221 / PR #244 requires field 0 to be a decimal
 *   cap_subject_id_t and field 1 to be a registered name in
 *   cdm_cap_names[]. The least-blast-radius resolution is to reuse
 *   CAP_APP_EXEC (the cap the launcher gates spawn on) with the would-be
 *   subject in field 0 and the literal "proc_table_full" in the
 *   resource slot — no new capability_id_t, no marker grammar change,
 *   passes cap_deny_marker_validate() unchanged.
 *
 * This test:
 *
 *   1. Fills the process table to PROC_TABLE_MAX.
 *   2. Captures stdout while invoking the (PROC_TABLE_MAX + 1)st
 *      process_create with a sentinel subject (4242).
 *   3. Asserts:
 *        - The call returns PROC_ERR_TABLE_FULL with PID_INVALID.
 *        - Exactly one CAP:DENY line was emitted.
 *        - The line is byte-exactly
 *            "CAP:DENY:4242:app_exec:proc_table_full\n"
 *        - The line round-trips through cap_deny_marker_validate()
 *          with rc == 0 (the #221 conformance predicate).
 *   4. Additionally asserts that a *successful* create after freeing
 *      one slot does NOT emit any CAP:DENY marker (happy-path is silent).
 *
 * Launched by:
 *   build/scripts/test_process_create_table_full_deny_marker.sh,
 *   dispatched via `build/scripts/test.sh process_create_table_full_deny_marker`.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/proc/process.h"

static void die(const char *reason) {
  /* Always emit on stderr so a partially-captured stdout cannot eat
   * the FAIL marker. */
  fprintf(stderr, "TEST:FAIL:process_create_table_full_deny_marker:%s\n",
          reason);
  exit(1);
}

/* Capture stdout into a buffer for the duration of `fn`. Returns the
 * captured bytes (NUL-terminated) in `out`; truncates at `out_size - 1`. */
static void with_captured_stdout(void (*fn)(void *), void *ctx,
                                 char *out, size_t out_size) {
  fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  if (saved < 0) {
    die("dup_stdout");
  }
  char tmpl[] = "/tmp/proc_full_deny_marker.XXXXXX";
  int tmpfd = mkstemp(tmpl);
  if (tmpfd < 0) {
    die("mkstemp");
  }
  /* Unlink early so we leak nothing on a crash later in the run. */
  unlink(tmpl);
  if (dup2(tmpfd, STDOUT_FILENO) < 0) {
    die("dup2_redirect");
  }
  fn(ctx);
  fflush(stdout);
  /* Restore stdout. */
  if (dup2(saved, STDOUT_FILENO) < 0) {
    die("dup2_restore");
  }
  close(saved);
  /* Read back. */
  if (lseek(tmpfd, 0, SEEK_SET) < 0) {
    die("lseek_tmpfd");
  }
  ssize_t n = read(tmpfd, out, out_size - 1u);
  if (n < 0) {
    die("read_tmpfd");
  }
  out[n] = '\0';
  close(tmpfd);
}

typedef struct {
  cap_subject_id_t subject;
  proc_result_t rc;
  process_id_t out_pid;
} create_call_t;

static void do_create(void *ctx) {
  create_call_t *c = (create_call_t *)ctx;
  c->out_pid = (process_id_t)0xDEADBEEFu;
  c->rc = process_create(c->subject, NULL, &c->out_pid);
}

/* Count occurrences of "CAP:DENY:" in `s`. */
static size_t count_deny_markers(const char *s) {
  size_t n = 0u;
  const char *p = s;
  while ((p = strstr(p, "CAP:DENY:")) != NULL) {
    n++;
    p++;
  }
  return n;
}

static void check_table_full_emits_canonical_marker(void) {
  process_table_reset();

  /* Fill the table. */
  for (uint32_t i = 0; i < PROC_TABLE_MAX; ++i) {
    process_id_t pid = PID_INVALID;
    proc_result_t rc = process_create((cap_subject_id_t)(200u + i),
                                      NULL, &pid);
    if (rc != PROC_OK || pid == PID_INVALID) {
      die("fill_to_capacity");
    }
  }

  /* One more — must deny + emit. */
  create_call_t call = { .subject = (cap_subject_id_t)4242u };
  char captured[1024];
  with_captured_stdout(do_create, &call, captured, sizeof(captured));

  if (call.rc != PROC_ERR_TABLE_FULL) {
    die("overflow_create_did_not_signal_table_full");
  }
  if (call.out_pid != PID_INVALID) {
    die("overflow_create_did_not_clear_out_pid");
  }

  const char *expected =
      "CAP:DENY:4242:app_exec:proc_table_full\n";
  if (strcmp(captured, expected) != 0) {
    fprintf(stderr,
            "TEST:FAIL:process_create_table_full_deny_marker:wire_mismatch\n"
            "  expected: %s"
            "  captured: %s",
            expected, captured);
    exit(1);
  }

  if (count_deny_markers(captured) != 1u) {
    die("expected_exactly_one_deny_marker");
  }

  /* Round-trip the captured line through the #221 conformance validator. */
  char reason[128] = {0};
  int vrc = cap_deny_marker_validate(captured, reason, sizeof(reason));
  if (vrc != 0) {
    fprintf(stderr,
            "TEST:FAIL:process_create_table_full_deny_marker:validate_rc=%d:%s\n",
            vrc, reason);
    exit(1);
  }

  printf("TEST:PASS:process_create_table_full_deny_marker_canonical_shape\n");
}

/* The successful-create path MUST be silent — emission belongs only to
 * the exhaustion deny, never the happy path. Regression guard against a
 * future refactor that hoists the marker emit above the slot scan. */
static void check_success_emits_no_marker(void) {
  process_table_reset();

  /* Fill, then free one. */
  process_id_t pids[PROC_TABLE_MAX];
  for (uint32_t i = 0; i < PROC_TABLE_MAX; ++i) {
    pids[i] = PID_INVALID;
    if (process_create((cap_subject_id_t)(300u + i), NULL, &pids[i]) != PROC_OK) {
      die("refill_to_capacity");
    }
  }
  if (process_destroy(pids[0]) != PROC_OK) {
    die("free_one_slot");
  }

  create_call_t call = { .subject = (cap_subject_id_t)9999u };
  char captured[1024];
  with_captured_stdout(do_create, &call, captured, sizeof(captured));

  if (call.rc != PROC_OK) {
    die("post_free_create_failed");
  }
  if (call.out_pid == PID_INVALID) {
    die("post_free_create_returned_invalid_pid");
  }
  if (strstr(captured, "CAP:DENY:") != NULL) {
    fprintf(stderr,
            "TEST:FAIL:process_create_table_full_deny_marker:"
            "happy_path_emitted_marker:%s\n", captured);
    exit(1);
  }
  printf("TEST:PASS:process_create_table_full_deny_marker_happy_path_silent\n");
}

int main(void) {
  check_table_full_emits_canonical_marker();
  check_success_emits_no_marker();
  printf("TEST:PASS:process_create_table_full_deny_marker\n");
  return 0;
}
