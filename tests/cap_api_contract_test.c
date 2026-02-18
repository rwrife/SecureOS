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

  if (CAP_CONSOLE_WRITE != 1) {
    fail("cap_console_write_id_stability");
  }
  if (CAP_SERIAL_WRITE != 2) {
    fail("cap_serial_write_id_stability");
  }
  printf("TEST:PASS:cap_api_contract_id_stability\n");

  if (cap_check(0u, CAP_CONSOLE_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny_console");
  }
  if (cap_check(0u, CAP_SERIAL_WRITE) != CAP_ERR_MISSING) {
    fail("default_deny_serial");
  }
  printf("TEST:PASS:cap_api_contract_default_deny\n");

  if (cap_grant_for_tests(0u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_console_failed");
  }
  if (cap_check(0u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("allow_console_failed");
  }
  if (cap_check(0u, CAP_SERIAL_WRITE) != CAP_ERR_MISSING) {
    fail("allow_console_leaked_serial");
  }

  if (cap_grant_for_tests(0u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("grant_serial_failed");
  }
  if (cap_check(0u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("allow_serial_failed");
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
