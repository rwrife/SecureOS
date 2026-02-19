#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_table.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:capability_gate:%s\n", reason);
  exit(1);
}

int main(void) {
  size_t bytes_written = 999u;

  printf("TEST:START:capability_gate\n");

  cap_table_init();

  if (cap_console_write_gate(1u, "hello", &bytes_written) != CAP_ERR_MISSING) {
    fail("default_deny_console_missing");
  }
  if (cap_serial_write_gate(1u, "hello", &bytes_written) != CAP_ERR_MISSING) {
    fail("default_deny_serial_missing");
  }
  if (cap_debug_exit_gate(1u, 0) != CAP_ERR_MISSING) {
    fail("default_deny_debug_exit_missing");
  }
  if (bytes_written != 999u) {
    fail("default_deny_side_effect");
  }
  printf("TEST:PASS:capability_gate_default_deny\n");

  if (cap_table_grant(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("grant_console_failed");
  }
  if (cap_console_write_gate(1u, "secureos", &bytes_written) != CAP_OK) {
    fail("allow_console_after_grant_missing");
  }
  if (bytes_written != 8u) {
    fail("allow_console_after_grant_bad_count");
  }
  if (cap_serial_write_gate(1u, "secureos", &bytes_written) != CAP_ERR_MISSING) {
    fail("console_grant_leaked_serial");
  }
  if (cap_debug_exit_gate(1u, 0) != CAP_ERR_MISSING) {
    fail("console_grant_leaked_debug_exit");
  }
  printf("TEST:PASS:capability_gate_allow_after_grant\n");
  printf("TEST:PASS:capability_gate_console_allow_after_grant\n");

  if (cap_table_grant(1u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("grant_serial_failed");
  }
  if (cap_serial_write_gate(1u, "secureos", &bytes_written) != CAP_OK) {
    fail("allow_serial_after_grant_missing");
  }
  if (bytes_written != 8u) {
    fail("allow_serial_after_grant_bad_count");
  }
  if (cap_debug_exit_gate(1u, 0) != CAP_ERR_MISSING) {
    fail("serial_grant_leaked_debug_exit");
  }
  printf("TEST:PASS:capability_gate_serial_allow_after_grant\n");

  if (cap_table_grant(1u, CAP_DEBUG_EXIT) != CAP_OK) {
    fail("grant_debug_exit_failed");
  }
  if (cap_debug_exit_gate(1u, 33) != CAP_OK) {
    fail("allow_debug_exit_after_grant_missing");
  }
  printf("TEST:PASS:capability_gate_debug_exit_allow_after_grant\n");

  if (cap_table_revoke(1u, CAP_CONSOLE_WRITE) != CAP_OK) {
    fail("revoke_console_failed");
  }
  if (cap_console_write_gate(1u, "secureos", &bytes_written) != CAP_ERR_MISSING) {
    fail("revoke_console_restore_deny_missing");
  }
  if (cap_table_revoke(1u, CAP_SERIAL_WRITE) != CAP_OK) {
    fail("revoke_serial_failed");
  }
  if (cap_serial_write_gate(1u, "secureos", &bytes_written) != CAP_ERR_MISSING) {
    fail("revoke_serial_restore_deny_missing");
  }
  if (cap_table_revoke(1u, CAP_DEBUG_EXIT) != CAP_OK) {
    fail("revoke_debug_exit_failed");
  }
  if (cap_debug_exit_gate(1u, 33) != CAP_ERR_MISSING) {
    fail("revoke_debug_exit_restore_deny_missing");
  }
  printf("TEST:PASS:capability_gate_revoke_restores_deny\n");

  if (cap_console_write_gate(CAP_TABLE_MAX_SUBJECTS, "secureos", &bytes_written) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_console_not_rejected");
  }
  if (cap_serial_write_gate(CAP_TABLE_MAX_SUBJECTS, "secureos", &bytes_written) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_serial_not_rejected");
  }
  if (cap_debug_exit_gate(CAP_TABLE_MAX_SUBJECTS, 33) != CAP_ERR_SUBJECT_INVALID) {
    fail("invalid_subject_debug_exit_not_rejected");
  }
  printf("TEST:PASS:capability_gate_invalid_subject\n");

  return 0;
}
