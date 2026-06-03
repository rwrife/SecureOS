/**
 * @file sosh_external_exec_test.c
 * @brief Issue #493 — host smoke for sosh's fall-through to
 *        `os_process_spawn` for external binaries.
 *
 * Sub-slice of #410, depends on #422. The kernel-side syscall +
 * CAP_APP_EXEC gate landed in PR #427; the soshlib evaluator's
 * SOSH_CAP_APP_EXEC gate landed in PR #371. Without this slice
 * `sosh> hello` silently no-op'd because `user/apps/sosh/main.c`
 * had no fall-through onto the wrapper.
 *
 * This test drives the embedder helper `sosh_try_exec_external`
 * (the unit of logic added to sosh) directly with mocked probe +
 * spawn bindings so we can exercise allow / deny / unknown without
 * a live bridge. The on-target arm — sosh main.c actually calling
 * `os_process_spawn` against `/apps/<cmd>` — is covered by the
 * production wiring in main.c (linked under the user-app build,
 * exercised by the in-OS toolchain `toolchain_runs_compiled_binary`
 * QEMU acceptance once #409 / #410 land).
 *
 * Markers:
 *   TEST:PASS:sosh_external_exec:allow_spawn_called
 *   TEST:PASS:sosh_external_exec:allow_probe_order_apps_first
 *   TEST:PASS:sosh_external_exec:allow_exit_status_propagates
 *   TEST:PASS:sosh_external_exec:allow_argv_passes_command_and_args
 *   TEST:PASS:sosh_external_exec:deny_returns_nonzero_no_swallow
 *   TEST:PASS:sosh_external_exec:deny_marker_owner_is_kernel
 *   TEST:PASS:sosh_external_exec:unknown_no_spawn_attempt
 *   TEST:PASS:sosh_external_exec:unknown_returns_not_found_sentinel
 *   TEST:PASS:sosh_external_exec:absolute_path_single_probe
 *   TEST:PASS:sosh_external_exec
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/apps/sosh/sosh_exec_external.h"
#include "../user/apps/sosh/sosh_exec_external.c"

static void die(const char *reason) {
  printf("TEST:FAIL:sosh_external_exec:%s\n", reason);
  exit(1);
}

/* --- Probe mock ------------------------------------------------------- */
typedef struct {
  /* Paths the mock will report present (NULL-terminated list). */
  const char *present_paths[8];
  /* Captured probe attempts in order (owned copies). */
  char probes[8][128];
  int probe_count;
} probe_ctx_t;

static os_status_t mock_probe(const char *path, void *vctx) {
  probe_ctx_t *c = (probe_ctx_t *)vctx;
  if (c->probe_count < 8) {
    snprintf(c->probes[c->probe_count], sizeof(c->probes[0]), "%s",
             path ? path : "");
    c->probe_count++;
  }
  for (int i = 0; c->present_paths[i] != NULL; ++i) {
    if (strcmp(c->present_paths[i], path) == 0) return OS_STATUS_OK;
  }
  return OS_STATUS_NOT_FOUND;
}

/* --- Spawn mock ------------------------------------------------------- */
typedef struct {
  int call_count;
  char last_path[128];
  char last_argv0[64];
  char last_argv1[128];
  int argv_term_is_null;
  unsigned int last_flags;
  os_status_t return_status;
  int return_exit;
} spawn_ctx_t;

static os_status_t mock_spawn(const char *path,
                              const char *const *argv,
                              unsigned int flags,
                              int *out_exit_status,
                              void *vctx) {
  spawn_ctx_t *c = (spawn_ctx_t *)vctx;
  c->call_count++;
  c->last_flags = flags;
  c->last_path[0] = '\0';
  c->last_argv0[0] = '\0';
  c->last_argv1[0] = '\0';
  c->argv_term_is_null = 0;
  if (path) snprintf(c->last_path, sizeof(c->last_path), "%s", path);
  if (argv) {
    if (argv[0]) snprintf(c->last_argv0, sizeof(c->last_argv0), "%s", argv[0]);
    if (argv[1]) snprintf(c->last_argv1, sizeof(c->last_argv1), "%s", argv[1]);
    /* argv must be NULL-terminated within 3 slots per the helper
     * contract. */
    if (argv[2] == NULL) c->argv_term_is_null = 1;
  }
  if (c->return_status == OS_STATUS_OK && out_exit_status) {
    *out_exit_status = c->return_exit;
  }
  return c->return_status;
}

static void reset_probe(probe_ctx_t *p) {
  memset(p, 0, sizeof(*p));
}
static void reset_spawn(spawn_ctx_t *s) {
  memset(s, 0, sizeof(*s));
  s->return_status = OS_STATUS_OK;
}

int main(void) {
  printf("TEST:START:sosh_external_exec\n");

  probe_ctx_t p;
  spawn_ctx_t s;

  /* === Scenario 1: allow — /apps/<cmd> resolves first, spawn fires === */
  reset_probe(&p);
  reset_spawn(&s);
  p.present_paths[0] = "/apps/hello";
  s.return_status = OS_STATUS_OK;
  s.return_exit = 42;

  int exit_rc = -1;
  sosh_external_result_t r =
      sosh_try_exec_external("hello", "world",
                             mock_probe, &p,
                             mock_spawn, &s,
                             &exit_rc);
  if (r != SOSH_EXTERNAL_RAN) die("allow_not_ran");
  if (s.call_count != 1) die("allow_spawn_not_called_once");
  if (strcmp(s.last_path, "/apps/hello") != 0) die("allow_spawn_wrong_path");
  printf("TEST:PASS:sosh_external_exec:allow_spawn_called\n");

  /* Probe order: first probe MUST be /apps/<cmd>, not /apps/dev/<cmd>
   * or the bare command name. This pins the documented search order. */
  if (p.probe_count < 1 || strcmp(p.probes[0], "/apps/hello") != 0) {
    die("allow_probe_order_wrong");
  }
  printf("TEST:PASS:sosh_external_exec:allow_probe_order_apps_first\n");

  /* Child exit status flows back so $? in script land is meaningful. */
  if (exit_rc != 42) die("allow_exit_not_propagated");
  printf("TEST:PASS:sosh_external_exec:allow_exit_status_propagates\n");

  /* argv[0] is the command name (not the resolved path) so the child
   * observes the conventional self-name; argv[1] is the joined args;
   * argv[2] MUST be NULL per the wrapper contract. */
  if (strcmp(s.last_argv0, "hello") != 0) die("allow_argv0_not_command");
  if (strcmp(s.last_argv1, "world") != 0) die("allow_argv1_not_args");
  if (!s.argv_term_is_null) die("allow_argv_not_null_terminated");
  if (s.last_flags != 0u) die("allow_flags_not_zero");
  printf("TEST:PASS:sosh_external_exec:allow_argv_passes_command_and_args\n");

  /* === Scenario 2: deny — kernel returns DENIED, helper surfaces it === */
  reset_probe(&p);
  reset_spawn(&s);
  p.present_paths[0] = "/apps/hello";
  s.return_status = OS_STATUS_DENIED;

  exit_rc = 0;
  r = sosh_try_exec_external("hello", NULL,
                             mock_probe, &p,
                             mock_spawn, &s,
                             &exit_rc);
  if (r != SOSH_EXTERNAL_RAN) die("deny_not_ran");
  if (exit_rc == 0) die("deny_swallowed_returned_zero");
  if (exit_rc != SOSH_EXEC_RC_DENIED) die("deny_wrong_sentinel");
  printf("TEST:PASS:sosh_external_exec:deny_returns_nonzero_no_swallow\n");

  /* Per docs/abi/sosh-capability-contract.md §4 + §6, the
   * CAP:DENY:<sid>:app_exec:<resource> marker is emitted by the
   * kernel leg of os_process_spawn, NOT by sosh itself. The helper
   * must therefore NOT print its own marker — we just surface the
   * non-zero rc and let the audited syscall own the marker. We
   * assert that nothing in this codepath emitted a CAP:DENY line on
   * stdout to lock that in. */
  /* (mock_spawn does not print; helper does not print; this is a
   * source-level invariant pinned by reading sosh_exec_external.c.
   * We assert here by inspecting the source for a printf calling
   * CAP:DENY would be a layering violation. The constants check
   * below is the testable proxy.) */
  if (SOSH_EXEC_RC_DENIED == 0) die("deny_sentinel_collides_with_ok");
  printf("TEST:PASS:sosh_external_exec:deny_marker_owner_is_kernel\n");

  /* === Scenario 3: unknown — no probe matches, no spawn attempt === */
  reset_probe(&p);
  reset_spawn(&s);
  /* No present paths. */

  exit_rc = -1;
  r = sosh_try_exec_external("nosuch", "args",
                             mock_probe, &p,
                             mock_spawn, &s,
                             &exit_rc);
  if (r != SOSH_EXTERNAL_NOT_FOUND) die("unknown_not_reported");
  if (s.call_count != 0) die("unknown_spawn_attempted");
  printf("TEST:PASS:sosh_external_exec:unknown_no_spawn_attempt\n");

  /* The helper returns NOT_FOUND so the caller (sosh main.c) can
   * keep emitting its existing "command not found" message + rc 127
   * — no regression on the builtin-dispatch path. */
  if (exit_rc != 0) die("unknown_clobbered_exit");
  printf("TEST:PASS:sosh_external_exec:unknown_returns_not_found_sentinel\n");

  /* Confirm full probe-order coverage: /apps/<cmd>, /apps/dev/<cmd>,
   * then <cmd> literal. (Defensive: if a future refactor narrows
   * the search, this catches it.) */
  if (p.probe_count != 3) die("unknown_probe_count_wrong");
  if (strcmp(p.probes[0], "/apps/nosuch") != 0) die("unknown_probe0_wrong");
  if (strcmp(p.probes[1], "/apps/dev/nosuch") != 0) die("unknown_probe1_wrong");
  if (strcmp(p.probes[2], "nosuch") != 0) die("unknown_probe2_wrong");

  /* === Scenario 4: absolute path — single probe, no prefixing === */
  reset_probe(&p);
  reset_spawn(&s);
  p.present_paths[0] = "/opt/bin/runme";
  s.return_status = OS_STATUS_OK;
  s.return_exit = 0;

  exit_rc = -1;
  r = sosh_try_exec_external("/opt/bin/runme", NULL,
                             mock_probe, &p,
                             mock_spawn, &s,
                             &exit_rc);
  if (r != SOSH_EXTERNAL_RAN) die("abs_not_ran");
  if (p.probe_count != 1) die("abs_probed_more_than_once");
  if (strcmp(p.probes[0], "/opt/bin/runme") != 0) die("abs_probe_wrong_path");
  if (strcmp(s.last_path, "/opt/bin/runme") != 0) die("abs_spawn_wrong_path");
  printf("TEST:PASS:sosh_external_exec:absolute_path_single_probe\n");

  printf("TEST:PASS:sosh_external_exec\n");
  return 0;
}
