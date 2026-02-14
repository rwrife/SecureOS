#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_api_contract:%s\n", reason);
  exit(1);
}

int main(void) {
  printf("TEST:START:cap_api_contract\n");

  cap_reset_for_tests();

  if (cap_check(0u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny");
  }
  printf("TEST:PASS:cap_api_contract_default_deny\n");

  if (cap_grant_for_tests(0u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_failed");
  }
  if (cap_check(0u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("allow_failed");
  }
  printf("TEST:PASS:cap_api_contract_allow\n");

  if (cap_check(999u, CAP_CONSOLE_WRITE) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject");
  }
  if (cap_check(0u, (capability_id_t)999u) != CAP_ERR_CAP_INVALID) {
    fail("invalid_cap");
  }
  printf("TEST:PASS:cap_api_contract_invalid_inputs\n");

  return 0;
}
