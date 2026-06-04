/**
 * @file app_native_process_spawn_deny_marker_test.c
 * @brief Pins the canonical `CAP:DENY:<sid>:app_exec:<resource>\n`
 *        marker emitted by the `app_native_process_spawn` bridge slot
 *        (`kernel/user/launcher_exec.c`, M7-TOOLCHAIN-003 #422 / PR
 *        #427) when the calling subject lacks `CAP_APP_EXEC`.
 *
 * Issue #532. The launcher deliberately emits the deny marker
 * *before* `process_run` touches the filesystem so the audit-ring
 * scanner has a stable `app_exec:<resource>` line for the
 * `launch.denied` invariant in plan #403 P4 (BUILD_ROADMAP §5.2).
 * Without a host-link pin, that load-bearing marker could silently
 * regress — same gap shape #487 / #503 / #508 / #512 / #514 closed
 * for other substrate subsystems.
 *
 * We exercise the production emission path via the
 * `app_native_spawn_cap_check` seam (`kernel/user/app_native_spawn.c`)
 * — the same fixture-seam pattern PR #495 used to host-link pin
 * `app_native_mem_brk` (#421 / #495). The seam is the exact body the
 * launcher invokes in production; this test compiles it in directly
 * (no stub) so a refactor that drops the marker emit will fail here.
 *
 * Pins:
 *   1. CAP_APP_EXEC granted → returns 0, emits nothing.
 *   2. CAP_APP_EXEC missing → returns 1, emits byte-exactly
 *      `CAP:DENY:<sid>:app_exec:/apps/hello.bin\n` and the line
 *      round-trips through `cap_deny_marker_validate()`.
 *   3. Sanitizer sub-check: pathological path containing ':' and
 *      '\n' (and a non-printable byte for good measure) is rewritten
 *      to '_' in the resource field — the canonical marker remains
 *      parseable.
 *   4. NULL / empty path produces a single '_' resource (defends the
 *      total-deny-emit contract; production callers reject these
 *      upstream and never reach the seam in practice).
 *
 * Launched by:
 *   build/scripts/test_app_native_process_spawn_deny_marker.sh,
 *   dispatched via
 *   `build/scripts/test.sh app_native_process_spawn_deny_marker`,
 *   wired into `validate_bundle.sh TEST_TARGETS`.
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
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/user/app_native_spawn.h"

static void die(const char *reason) {
  fprintf(stderr,
          "TEST:FAIL:app_native_process_spawn_deny_marker:%s\n", reason);
  exit(1);
}

/* Capture stdout for the duration of `fn(ctx)`. */
static void with_captured_stdout(void (*fn)(void *), void *ctx,
                                 char *out, size_t out_size) {
  fflush(stdout);
  int saved = dup(STDOUT_FILENO);
  if (saved < 0) {
    die("dup_stdout");
  }
  char tmpl[] = "/tmp/app_spawn_deny_marker.XXXXXX";
  int tmpfd = mkstemp(tmpl);
  if (tmpfd < 0) {
    die("mkstemp");
  }
  unlink(tmpl);
  if (dup2(tmpfd, STDOUT_FILENO) < 0) {
    die("dup2_redirect");
  }
  fn(ctx);
  fflush(stdout);
  if (dup2(saved, STDOUT_FILENO) < 0) {
    die("dup2_restore");
  }
  close(saved);
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
  const char *path;
  int rc;
} spawn_call_t;

static void do_check(void *ctx) {
  spawn_call_t *c = (spawn_call_t *)ctx;
  c->rc = app_native_spawn_cap_check(c->subject, c->path);
}

static size_t count_deny_markers(const char *s) {
  size_t n = 0u;
  const char *p = s;
  while ((p = strstr(p, "CAP:DENY:")) != NULL) {
    n++;
    p++;
  }
  return n;
}

static void expect_canonical(const char *captured, const char *expected,
                             const char *tag) {
  if (strcmp(captured, expected) != 0) {
    fprintf(stderr,
            "TEST:FAIL:app_native_process_spawn_deny_marker:%s_wire_mismatch\n"
            "  expected: %s"
            "  captured: %s",
            tag, expected, captured);
    exit(1);
  }
  if (count_deny_markers(captured) != 1u) {
    fprintf(stderr,
            "TEST:FAIL:app_native_process_spawn_deny_marker:"
            "%s_expected_one_marker:got=%zu\n",
            tag, count_deny_markers(captured));
    exit(1);
  }
  char reason[128] = {0};
  int vrc = cap_deny_marker_validate(captured, reason, sizeof(reason));
  if (vrc != 0) {
    fprintf(stderr,
            "TEST:FAIL:app_native_process_spawn_deny_marker:"
            "%s_validate_rc=%d:%s\n",
            tag, vrc, reason);
    exit(1);
  }
}

/* 1+2: grant → silent + return 0; deny → return 1 + canonical marker. */
static void check_grant_silent_and_deny_canonical(void) {
  cap_table_reset();

  /* Grant path — must be totally silent (no CAP:DENY) and return 0.
   * Note: cap_table is sized for CAP_TABLE_MAX_SUBJECTS slots; we use
   * a small subject id here. The `proc_table_full` peer test (#261)
   * uses sid=4242 because it goes through the marker formatter only
   * (not cap_table_grant), so its subject id is not constrained. */
  if (cap_table_grant((cap_subject_id_t)3u, CAP_APP_EXEC) != CAP_OK) {
    die("grant_cap_app_exec");
  }
  {
    spawn_call_t call = { .subject = 3u, .path = "/apps/hello.bin" };
    char captured[1024];
    with_captured_stdout(do_check, &call, captured, sizeof(captured));
    if (call.rc != 0) {
      die("granted_subject_did_not_return_zero");
    }
    if (strstr(captured, "CAP:DENY:") != NULL) {
      fprintf(stderr,
              "TEST:FAIL:app_native_process_spawn_deny_marker:"
              "granted_path_emitted_marker:%s\n", captured);
      exit(1);
    }
  }

  /* Deny path — fresh subject with no caps. */
  cap_table_reset();
  {
    spawn_call_t call = { .subject = 5u, .path = "/apps/hello.bin" };
    char captured[1024];
    with_captured_stdout(do_check, &call, captured, sizeof(captured));
    if (call.rc != 1) {
      die("missing_cap_did_not_return_one");
    }
    const char *expected =
        "CAP:DENY:5:app_exec:/apps/hello.bin\n";
    expect_canonical(captured, expected, "deny");
  }

  printf("TEST:PASS:app_native_process_spawn_deny_marker_canonical_shape\n");
}

/* 3: sanitizer rewrites ':' + '\n' + non-printable bytes to '_'. The
 * sanitized resource must produce a parseable marker. */
static void check_sanitizer_rewrites_forbidden_bytes(void) {
  cap_table_reset();
  /* Path contains:
   *   - a colon ':'  (cap_deny_marker_format would reject this in the
   *                   resource field — must be sanitized away),
   *   - a newline '\n' (would terminate the marker prematurely),
   *   - a non-printable 0x01 byte.
   * Expected: each of those three bytes becomes '_' in the resource.
   * Wrapping ASCII (`/x` and `y/z.bin`) survives unchanged. */
  const char *path = "/x:y\n\x01z.bin";
  spawn_call_t call = { .subject = 7u, .path = path };
  char captured[1024];
  with_captured_stdout(do_check, &call, captured, sizeof(captured));

  if (call.rc != 1) {
    die("sanitizer_path_did_not_deny");
  }

  const char *expected =
      "CAP:DENY:7:app_exec:/x_y__z.bin\n";
  expect_canonical(captured, expected, "sanitizer");

  printf("TEST:PASS:app_native_process_spawn_deny_marker_sanitizer\n");
}

/* 4: NULL and "" paths still emit a parseable marker with '_' as the
 * resource. Defends the helper's "always emit on deny" invariant; the
 * launcher itself rejects these upstream with rc=3 so this is a
 * belt-and-braces pin on the seam. */
static void check_empty_path_emits_underscore_resource(void) {
  cap_table_reset();
  {
    spawn_call_t call = { .subject = 2u, .path = "" };
    char captured[1024];
    with_captured_stdout(do_check, &call, captured, sizeof(captured));
    if (call.rc != 1) {
      die("empty_path_did_not_deny");
    }
    expect_canonical(captured, "CAP:DENY:2:app_exec:_\n", "empty");
  }
  {
    spawn_call_t call = { .subject = 2u, .path = NULL };
    char captured[1024];
    with_captured_stdout(do_check, &call, captured, sizeof(captured));
    if (call.rc != 1) {
      die("null_path_did_not_deny");
    }
    expect_canonical(captured, "CAP:DENY:2:app_exec:_\n", "null");
  }
  printf("TEST:PASS:app_native_process_spawn_deny_marker_empty_path\n");
}

int main(void) {
  check_grant_silent_and_deny_canonical();
  check_sanitizer_rewrites_forbidden_bytes();
  check_empty_path_emits_underscore_resource();
  printf("TEST:PASS:app_native_process_spawn_deny_marker\n");
  return 0;
}
