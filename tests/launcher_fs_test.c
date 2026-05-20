/**
 * @file launcher_fs_test.c
 * @brief Tests for the launcher-mediated filesystem capability slice.
 *
 * Purpose:
 *   Verifies the slice from plans/2026-04-16-filesystem-service-faux-fs.md:
 *     - deny-by-default read/write
 *     - launcher-mediated grant + revoke
 *     - persistent vs ephemeral lifecycle behavior across relaunch
 *     - cross-app isolation
 *     - bypass attempts (unregistered subjects, missing grant) fail closed
 *
 * Interactions:
 *   - launcher_fs.c: exercises the launcher-mediated FS API end to end.
 *   - cap_table.c: relied on transitively for the capability checks.
 *
 * Launched by:
 *   Compiled and run by the test harness
 *   (build/scripts/test_launcher_fs.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/user/launcher_fs.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_fs:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  cap_table_reset();
  launcher_fs_reset();
}

static void test_deny_without_grant(void) {
  reset_world();
  if (launcher_fs_register_app(1u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail("register_persistent");
  }
  if (launcher_fs_app_has_read(1u) || launcher_fs_app_has_write(1u)) {
    fail("default_grants_present");
  }
  if (launcher_fs_app_write(1u, "notes.txt", "hello") != LAUNCHER_FS_ERR_DENIED) {
    fail("write_should_deny_without_grant");
  }
  char buf[32];
  size_t n = 0;
  if (launcher_fs_app_read(1u, "notes.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_ERR_DENIED) {
    fail("read_should_deny_without_grant");
  }
  printf("TEST:PASS:launcher_fs_deny_without_grant\n");
}

static void test_allow_after_grant(void) {
  reset_world();
  if (launcher_fs_register_app(2u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail("register");
  }
  if (launcher_fs_grant_write(2u) != LAUNCHER_FS_OK) {
    fail("grant_write");
  }
  if (launcher_fs_grant_read(2u) != LAUNCHER_FS_OK) {
    fail("grant_read");
  }
  if (launcher_fs_app_write(2u, "doc.txt", "abc") != LAUNCHER_FS_OK) {
    fail("write_after_grant");
  }
  char buf[16];
  size_t n = 0;
  if (launcher_fs_app_read(2u, "doc.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
    fail("read_after_grant");
  }
  if (n != 3 || strcmp(buf, "abc") != 0) {
    fail("read_content");
  }
  printf("TEST:PASS:launcher_fs_allow_after_grant\n");
}

static void test_revoke_restores_deny(void) {
  reset_world();
  if (launcher_fs_register_app(3u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) {
    fail("register");
  }
  if (launcher_fs_grant_write(3u) != LAUNCHER_FS_OK) fail("grant");
  if (launcher_fs_app_write(3u, "f", "x") != LAUNCHER_FS_OK) fail("write");
  if (launcher_fs_revoke_write(3u) != LAUNCHER_FS_OK) fail("revoke");
  if (launcher_fs_app_write(3u, "f", "y") != LAUNCHER_FS_ERR_DENIED) {
    fail("write_after_revoke_should_deny");
  }
  printf("TEST:PASS:launcher_fs_revoke_restores_deny\n");
}

static void test_persistent_survives_relaunch(void) {
  reset_world();
  if (launcher_fs_register_app(4u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) fail("register");
  if (launcher_fs_grant_write(4u) != LAUNCHER_FS_OK) fail("grant_w");
  if (launcher_fs_grant_read(4u) != LAUNCHER_FS_OK) fail("grant_r");
  if (launcher_fs_app_write(4u, "p.txt", "persist") != LAUNCHER_FS_OK) fail("write");

  if (launcher_fs_app_relaunch(4u) != LAUNCHER_FS_OK) fail("relaunch");
  /* Grants must NOT survive relaunch -- launcher must re-issue. */
  if (launcher_fs_app_has_read(4u) || launcher_fs_app_has_write(4u)) {
    fail("grants_should_clear_on_relaunch");
  }
  /* Data must still exist; reissue read grant and verify. */
  if (launcher_fs_grant_read(4u) != LAUNCHER_FS_OK) fail("regrant");
  char buf[32];
  size_t n = 0;
  if (launcher_fs_app_read(4u, "p.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_OK) {
    fail("persistent_data_lost");
  }
  if (n != 7 || strcmp(buf, "persist") != 0) fail("persist_content");
  printf("TEST:PASS:launcher_fs_persistent_survives_relaunch\n");
}

static void test_ephemeral_resets_on_relaunch(void) {
  reset_world();
  if (launcher_fs_register_app(5u, LAUNCHER_FS_MODE_EPHEMERAL) != LAUNCHER_FS_OK) fail("register");
  if (launcher_fs_grant_write(5u) != LAUNCHER_FS_OK) fail("grant_w");
  if (launcher_fs_grant_read(5u) != LAUNCHER_FS_OK) fail("grant_r");
  if (launcher_fs_app_write(5u, "tmp.txt", "scratch") != LAUNCHER_FS_OK) fail("write");
  char buf[32];
  size_t n = 0;
  if (launcher_fs_app_read(5u, "tmp.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_OK) fail("read");

  if (launcher_fs_app_relaunch(5u) != LAUNCHER_FS_OK) fail("relaunch");
  if (launcher_fs_grant_read(5u) != LAUNCHER_FS_OK) fail("regrant_r");
  /* Ephemeral data must NOT survive a relaunch. */
  if (launcher_fs_app_read(5u, "tmp.txt", buf, sizeof(buf), &n) != LAUNCHER_FS_ERR_NOT_FOUND) {
    fail("ephemeral_should_reset_on_relaunch");
  }
  printf("TEST:PASS:launcher_fs_ephemeral_resets_on_relaunch\n");
}

static void test_cross_app_isolation(void) {
  reset_world();
  if (launcher_fs_register_app(1u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) fail("reg1");
  if (launcher_fs_register_app(2u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) fail("reg2");
  if (launcher_fs_grant_write(1u) != LAUNCHER_FS_OK) fail("g1w");
  if (launcher_fs_grant_read(1u) != LAUNCHER_FS_OK) fail("g1r");
  if (launcher_fs_grant_read(2u) != LAUNCHER_FS_OK) fail("g2r");
  if (launcher_fs_app_write(1u, "secret", "alpha") != LAUNCHER_FS_OK) fail("write1");

  /* App 2 has read but it is on its own namespace -- must not see app 1's data. */
  char buf[32];
  size_t n = 0;
  if (launcher_fs_app_read(2u, "secret", buf, sizeof(buf), &n) != LAUNCHER_FS_ERR_NOT_FOUND) {
    fail("cross_app_read_should_not_find");
  }
  /* App 2 still has no write capability either. */
  if (launcher_fs_app_write(2u, "secret", "beta") != LAUNCHER_FS_ERR_DENIED) {
    fail("cross_app_write_should_deny");
  }
  printf("TEST:PASS:launcher_fs_cross_app_isolation\n");
}

static void test_bypass_unregistered_denied(void) {
  reset_world();
  /* Subject not registered with the launcher cannot use the API even if a
   * stray cap_table grant somehow appeared. */
  if (cap_table_grant(6u, CAP_FS_WRITE) != CAP_OK) fail("seed_grant");
  if (launcher_fs_app_write(6u, "x", "y") != LAUNCHER_FS_ERR_NOT_REGISTERED) {
    fail("unregistered_should_be_blocked");
  }
  /* And the launcher's grant API also rejects unregistered subjects. */
  if (launcher_fs_grant_write(6u) != LAUNCHER_FS_ERR_NOT_REGISTERED) {
    fail("grant_unregistered");
  }
  printf("TEST:PASS:launcher_fs_bypass_unregistered_denied\n");
}

static void test_invalid_inputs(void) {
  reset_world();
  if (launcher_fs_register_app(99u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_ERR_INVALID_APP) {
    fail("oob_subject");
  }
  if (launcher_fs_register_app(0u, (launcher_fs_mode_t)42) != LAUNCHER_FS_ERR_INVALID_ARG) {
    fail("invalid_mode");
  }
  if (launcher_fs_register_app(0u, LAUNCHER_FS_MODE_PERSISTENT) != LAUNCHER_FS_OK) fail("reg0");
  if (launcher_fs_register_app(0u, LAUNCHER_FS_MODE_EPHEMERAL) != LAUNCHER_FS_ERR_INVALID_ARG) {
    fail("mode_flip_should_reject");
  }
  printf("TEST:PASS:launcher_fs_invalid_inputs\n");
}

int main(void) {
  printf("TEST:START:launcher_fs\n");
  test_deny_without_grant();
  test_allow_after_grant();
  test_revoke_restores_deny();
  test_persistent_survives_relaunch();
  test_ephemeral_resets_on_relaunch();
  test_cross_app_isolation();
  test_bypass_unregistered_denied();
  test_invalid_inputs();
  printf("TEST:PASS:launcher_fs\n");
  return 0;
}
