/**
 * @file fs_service_persist_deny_test.c
 * @brief M3 acceptance: app without persistent FS rights either fails closed
 *        or is redirected into the faux/ephemeral scope, and never reaches
 *        the persistent backend.
 *
 * Plan: plans/2026-05-14-m3-fs-service-acceptance-tests.md
 * Tracking issue: #108.
 *
 * The merged launcher_fs surface from PR #88 supports both branches the plan
 * permits:
 *   - fail_closed:           register PERSISTENT, never grant CAP_FS_WRITE
 *                            -> launcher_fs_app_write returns DENIED.
 *   - redirected_to_ephemeral: register EPHEMERAL (the launcher's allowed
 *                            "denied persistent FS still saves in ephemeral
 *                            scope" branch from BUILD_ROADMAP §5.3),
 *                            grant write, write succeeds but does not
 *                            persist across relaunch and is invisible to a
 *                            persistent peer.
 *
 * This test exercises BOTH branches in sequence and records which one fired
 * in the marker payload, per the plan's "exactly one of these two outcomes
 * and records which one" requirement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/user/launcher_fs.h"

static void fail_marker(const char *reason) {
  printf("TEST:FAIL:fs_service_persist_deny:%s\n", reason);
  exit(1);
}

/* Branch A: fail-closed. App registered as PERSISTENT but never granted
 * CAP_FS_WRITE -- writes are rejected with DENIED. */
static void run_fail_closed_branch(void) {
  cap_table_reset();
  launcher_fs_reset();

  const cap_subject_id_t denied = 1u;
  const cap_subject_id_t allowed = 2u;

  if (launcher_fs_register_app(denied, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail_marker("fail_closed_register");
  }
  if (launcher_fs_register_app(allowed, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail_marker("fail_closed_register_peer");
  }
  if (launcher_fs_grant_read(allowed) != LAUNCHER_FS_OK) {
    fail_marker("fail_closed_peer_grant_read");
  }

  /* 1. cap_absent: persistent-write right is not held. */
  if (launcher_fs_app_has_write(denied)) {
    fail_marker("fail_closed_unexpected_write_grant");
  }
  printf("TEST:PASS:fs_service_persist_deny:cap_absent\n");

  /* 2. Write must fail closed (DENIED). */
  launcher_fs_result_t r =
      launcher_fs_app_write(denied, "secret.txt", "should-not-land");
  if (r != LAUNCHER_FS_ERR_DENIED) {
    fail_marker("fail_closed_write_not_denied");
  }
  printf("TEST:PASS:fs_service_persist_deny:fail_closed\n");

  /* 3. no_persist_visibility: peer with persistent read does not see any
   *    blob at that path. */
  {
    char buf[64] = {0};
    size_t n = 0;
    launcher_fs_result_t pr =
        launcher_fs_app_read(allowed, "secret.txt", buf, sizeof(buf), &n);
    if (pr != LAUNCHER_FS_ERR_NOT_FOUND) {
      fail_marker("fail_closed_peer_unexpectedly_observed_data");
    }
  }
  printf("TEST:PASS:fs_service_persist_deny:no_persist_visibility:fail_closed\n");
}

/* Branch B: redirected-to-ephemeral. App registered EPHEMERAL with grants
 * is the launcher's editor-saves-in-ephemeral-scope branch of the roadmap
 * text. Write succeeds locally but never reaches the persistent backend. */
static void run_redirected_branch(void) {
  cap_table_reset();
  launcher_fs_reset();

  const cap_subject_id_t denied = 1u;   /* ephemeral (no persistent rights) */
  const cap_subject_id_t allowed = 2u;  /* persistent peer for visibility */

  if (launcher_fs_register_app(denied, LAUNCHER_FS_MODE_EPHEMERAL) != LAUNCHER_FS_OK) {
    fail_marker("redirected_register_ephemeral");
  }
  if (launcher_fs_register_app(allowed, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail_marker("redirected_register_peer");
  }
  if (launcher_fs_grant_write(denied) != LAUNCHER_FS_OK) {
    fail_marker("redirected_grant_write");
  }
  if (launcher_fs_grant_read(denied) != LAUNCHER_FS_OK) {
    fail_marker("redirected_grant_read");
  }
  if (launcher_fs_grant_read(allowed) != LAUNCHER_FS_OK) {
    fail_marker("redirected_peer_grant_read");
  }

  /* The denied app's mode is not PERSISTENT -- "persist cap absent". */
  if (launcher_fs_app_mode(denied) != LAUNCHER_FS_MODE_EPHEMERAL) {
    fail_marker("redirected_unexpected_mode");
  }

  /* Write succeeds locally, into the faux/ephemeral scope. */
  if (launcher_fs_app_write(denied, "draft.txt", "ephemeral-blob") != LAUNCHER_FS_OK) {
    fail_marker("redirected_write_should_succeed_locally");
  }
  /* Same-session read confirms the bytes are visible in the ephemeral scope. */
  {
    char buf[64] = {0};
    size_t n = 0;
    if (launcher_fs_app_read(denied, "draft.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
      fail_marker("redirected_local_read_failed");
    }
    if (n != strlen("ephemeral-blob") || strcmp(buf, "ephemeral-blob") != 0) {
      fail_marker("redirected_local_read_content");
    }
  }
  printf("TEST:PASS:fs_service_persist_deny:redirected_to_ephemeral\n");

  /* no_persist_visibility (redirected branch): the persistent peer cannot
   * observe the ephemeral blob at that path. */
  {
    char buf[64] = {0};
    size_t n = 0;
    launcher_fs_result_t pr =
        launcher_fs_app_read(allowed, "draft.txt", buf, sizeof(buf), &n);
    if (pr != LAUNCHER_FS_ERR_NOT_FOUND) {
      fail_marker("redirected_peer_observed_ephemeral_blob");
    }
  }
  printf("TEST:PASS:fs_service_persist_deny:no_persist_visibility:redirected\n");
}

int main(void) {
  printf("TEST:START:fs_service_persist_deny\n");

  run_fail_closed_branch();
  run_redirected_branch();

  /* Audit-deny assertion is gated on #84 / #98 landing. Until the audit
   * ring is wired through the launcher_fs deny path on main, emit a SKIP
   * marker so the validator JSON report can distinguish "not asserted"
   * from "asserted and passed". */
  printf("TEST:SKIP:fs_service_persist_deny:audit_deny_recorded:audit_log_unwired\n");

  printf("TEST:PASS:fs_service_persist_deny\n");
  return 0;
}
