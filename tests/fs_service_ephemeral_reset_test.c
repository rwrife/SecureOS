/**
 * @file fs_service_ephemeral_reset_test.c
 * @brief M3 acceptance: data written under the faux/ephemeral scope does
 *        not survive an app exit/relaunch cycle, and never spills into a
 *        persistent peer's namespace.
 *
 * Plan: plans/2026-05-14-m3-fs-service-acceptance-tests.md
 * Tracking issue: #108.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/user/launcher_fs.h"

static void fail_marker(const char *reason) {
  printf("TEST:FAIL:fs_service_ephemeral_reset:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:fs_service_ephemeral_reset\n");
  cap_table_reset();
  launcher_fs_reset();

  const cap_subject_id_t ephemeral_app = 1u;
  const cap_subject_id_t persistent_peer = 2u;
  const char *path = "scratch.txt";
  const char *blob = "ephemeral-payload";
  const size_t blob_len = strlen(blob);

  if (launcher_fs_register_app(ephemeral_app, LAUNCHER_FS_MODE_EPHEMERAL) != LAUNCHER_FS_OK) {
    fail_marker("register_ephemeral");
  }
  if (launcher_fs_register_app(persistent_peer, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail_marker("register_peer");
  }
  if (launcher_fs_grant_write(ephemeral_app) != LAUNCHER_FS_OK) fail_marker("grant_w");
  if (launcher_fs_grant_read(ephemeral_app) != LAUNCHER_FS_OK) fail_marker("grant_r");
  if (launcher_fs_grant_read(persistent_peer) != LAUNCHER_FS_OK) fail_marker("peer_grant_r");

  /* 1. write_to_faux_succeeds: write under ephemeral mode reports OK; an
   *    immediate read returns the same bytes. */
  if (launcher_fs_app_write(ephemeral_app, path, blob) != LAUNCHER_FS_OK) {
    fail_marker("faux_write_failed");
  }
  {
    char buf[64] = {0};
    size_t n = 0;
    if (launcher_fs_app_read(ephemeral_app, path, buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
      fail_marker("faux_immediate_read_failed");
    }
    if (n != blob_len || strcmp(buf, blob) != 0) {
      fail_marker("faux_immediate_read_content_mismatch");
    }
  }
  printf("TEST:PASS:fs_service_ephemeral_reset:write_to_faux_succeeds\n");

  /* 2. visible_in_same_session: a second open of the same path within the
   *    same session reads the blob. */
  {
    char buf[64] = {0};
    size_t n = 0;
    if (launcher_fs_app_read(ephemeral_app, path, buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
      fail_marker("faux_second_read_failed");
    }
    if (n != blob_len || strcmp(buf, blob) != 0) {
      fail_marker("faux_second_read_content_mismatch");
    }
  }
  printf("TEST:PASS:fs_service_ephemeral_reset:visible_in_same_session\n");

  /* 3. gone_after_relaunch: simulate app exit/relaunch via the launcher's
   *    relaunch hook; the blob is gone. */
  if (launcher_fs_app_relaunch(ephemeral_app) != LAUNCHER_FS_OK) {
    fail_marker("relaunch_failed");
  }
  /* Grants must be re-issued (PR #88 invariant). */
  if (launcher_fs_grant_read(ephemeral_app) != LAUNCHER_FS_OK) {
    fail_marker("regrant_read_after_relaunch");
  }
  {
    char buf[64] = {0};
    size_t n = 0;
    launcher_fs_result_t r =
        launcher_fs_app_read(ephemeral_app, path, buf, sizeof(buf), &n);
    if (r != LAUNCHER_FS_ERR_NOT_FOUND) {
      fail_marker("ephemeral_data_survived_relaunch");
    }
  }
  printf("TEST:PASS:fs_service_ephemeral_reset:gone_after_relaunch\n");

  /* 4. no_persist_leak: the persistent peer does not observe the ephemeral
   *    blob at that path -- ephemeral scope never spills upward. */
  {
    char buf[64] = {0};
    size_t n = 0;
    launcher_fs_result_t r =
        launcher_fs_app_read(persistent_peer, path, buf, sizeof(buf), &n);
    if (r != LAUNCHER_FS_ERR_NOT_FOUND) {
      fail_marker("ephemeral_leaked_to_persistent_peer");
    }
  }
  printf("TEST:PASS:fs_service_ephemeral_reset:no_persist_leak\n");

  printf("TEST:PASS:fs_service_ephemeral_reset\n");
  return 0;
}
