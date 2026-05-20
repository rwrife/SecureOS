/**
 * @file fs_service_persist_allow_test.c
 * @brief M3 acceptance: app with persistent FS grant writes a blob, sees it
 *        survive an app exit/relaunch cycle.
 *
 * Plan: plans/2026-05-14-m3-fs-service-acceptance-tests.md
 * Tracking issue: #108.
 *
 * Notes on surface mapping:
 *   The plan was authored before PR #88 / #83 froze the launcher_fs surface.
 *   The merged API gates "persistent" via a per-app storage mode set at
 *   registration (LAUNCHER_FS_MODE_PERSISTENT) rather than a standalone
 *   CAP_FS_PERSIST capability. We honor the plan's contract intent against
 *   the actually-merged API; the plan explicitly allows this ("this plan
 *   does not pin the spelling").
 *
 * Emits markers consumed by build/scripts/test_fs_service_persist_allow.sh
 * and surfaced in the validator JSON report (#110 / PR #112).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/user/launcher_fs.h"

static void fail_marker(const char *reason) {
  printf("TEST:FAIL:fs_service_persist_allow:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:fs_service_persist_allow\n");
  cap_table_reset();
  launcher_fs_reset();

  const cap_subject_id_t app = 1u;
  const char *path = "note.txt";
  const char *blob = "persisted-by-allow";
  const size_t blob_len = strlen(blob);

  /* 1. cap_present: the persistence-granting surface is in place. We assert
   *    the merged-API equivalent: registration as PERSISTENT mode succeeds
   *    and the per-mode getter reports PERSISTENT. */
  if (launcher_fs_register_app(app, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail_marker("register_persistent");
  }
  if (launcher_fs_app_mode(app) != LAUNCHER_FS_MODE_PERSISTENT) {
    fail_marker("mode_not_persistent");
  }
  printf("TEST:PASS:fs_service_persist_allow:cap_present\n");

  /* 2. write_succeeds: explicit grant + write returns OK. */
  if (launcher_fs_grant_write(app) != LAUNCHER_FS_OK) fail_marker("grant_write");
  if (launcher_fs_grant_read(app) != LAUNCHER_FS_OK) fail_marker("grant_read");
  if (launcher_fs_app_write(app, path, blob) != LAUNCHER_FS_OK) {
    fail_marker("write_returned_nonzero");
  }
  printf("TEST:PASS:fs_service_persist_allow:write_succeeds\n");

  /* 3. read_back_after_close: in-session read returns the same bytes. */
  {
    char buf[64] = {0};
    size_t n = 0;
    if (launcher_fs_app_read(app, path, buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
      fail_marker("read_after_write_failed");
    }
    if (n != blob_len || strcmp(buf, blob) != 0) {
      fail_marker("read_after_write_content_mismatch");
    }
  }
  printf("TEST:PASS:fs_service_persist_allow:read_back_after_close\n");

  /* 4. relaunch_round_trip: data survives an exit/relaunch; grants must
   *    be re-issued (launcher-mediated invariant from PR #88). */
  if (launcher_fs_app_relaunch(app) != LAUNCHER_FS_OK) {
    fail_marker("relaunch_failed");
  }
  if (launcher_fs_app_has_read(app) || launcher_fs_app_has_write(app)) {
    fail_marker("grants_should_clear_on_relaunch");
  }
  if (launcher_fs_grant_read(app) != LAUNCHER_FS_OK) {
    fail_marker("regrant_read_after_relaunch");
  }
  {
    char buf[64] = {0};
    size_t n = 0;
    if (launcher_fs_app_read(app, path, buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
      fail_marker("persistent_data_lost_across_relaunch");
    }
    if (n != blob_len || strcmp(buf, blob) != 0) {
      fail_marker("persistent_data_corrupted_across_relaunch");
    }
  }
  printf("TEST:PASS:fs_service_persist_allow:relaunch_round_trip\n");

  printf("TEST:PASS:fs_service_persist_allow\n");
  return 0;
}
