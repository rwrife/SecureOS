#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_table.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_table:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:capability_table\n");

  cap_table_init();

  for (cap_subject_id_t subject = 0; subject < CAP_TABLE_MAX_SUBJECTS; ++subject) {
    if (cap_table_check(subject, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
      fail("default_deny");
    }
  }
  printf("TEST:PASS:capability_table_default_deny\n");

  if (cap_table_grant(3u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_failed");
  }
  if (cap_table_check(3u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_allow_missing");
  }
  if (cap_table_check(2u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("grant_leaked_subject");
  }
  printf("TEST:PASS:capability_table_grant_allow\n");

  if (cap_table_revoke(3u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_failed");
  }
  if (cap_table_check(3u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("revoke_deny_missing");
  }
  printf("TEST:PASS:capability_table_revoke_deny\n");

  if (cap_table_grant(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_before_reset_failed");
  }
  cap_table_reset();
  if (cap_table_check(1u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("reset_default_deny_missing");
  }
  printf("TEST:PASS:capability_table_reset_clears_grants\n");

  if (cap_table_grant(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_grant");
  }
  if (cap_table_revoke(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_revoke");
  }
  if (cap_table_check(CAP_TABLE_MAX_SUBJECTS, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_check");
  }
  if (cap_table_grant(0u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap_grant");
  }
  if (cap_table_revoke(0u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap_revoke");
  }
  if (cap_table_check(0u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap_check");
  }
  printf("TEST:PASS:capability_table_invalid_inputs\n");

  return 0;
}
