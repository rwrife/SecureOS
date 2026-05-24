/**
 * @file console_svc_port_alloc_test.c
 * @brief Validator for slice 1 of plan #263 (issue #268).
 *
 * Asserts the contract documented in `kernel/svc/console_svc.h`:
 *
 *   1. Before init: console_svc_is_initialised() is false and
 *      console_svc_port() returns IPC_PORT_INVALID.
 *   2. After init: console_svc_is_initialised() is true,
 *      console_svc_port() returns a non-zero handle, the handle is
 *      owned by SUBJECT_M2_CONSOLE_SVC, and both its send_cap and
 *      recv_cap are CAP_CONSOLE_WRITE (the slice-1 convention — recv
 *      side gated by the owner check; see plan §"Console service
 *      module" for rationale).
 *   3. A second init without reset returns CONSOLE_SVC_ERR_ALREADY_INIT
 *      and does NOT allocate a second port (handle is unchanged).
 *   4. After console_svc_reset(): port is IPC_PORT_INVALID and
 *      is_initialised is false. A subsequent init succeeds and yields
 *      a fresh handle (post-reset, the previous handle's generation
 *      has been bumped by ipc_port_table_reset() so any stored copy
 *      now fails — covered by the IPC port lifecycle suite, not
 *      re-asserted here).
 *
 * Output markers (consumed by build/scripts/test_console_svc_port_alloc.sh):
 *   TEST:PASS:console_svc_port_alloc_uninit
 *   TEST:PASS:console_svc_port_alloc_init
 *   TEST:PASS:console_svc_port_alloc_double_init
 *   TEST:PASS:console_svc_port_alloc_reset
 *   TEST:PASS:console_svc_port_alloc
 *
 * Pure host-side, no kernel runtime dependencies beyond the slice-1
 * source files.
 *
 * Issue: #268. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 1.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/svc/console_svc.h"
#include "harness/m2_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:console_svc_port_alloc:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  console_svc_reset();
  ipc_port_table_reset();
}

static void test_uninit(void) {
  reset_world();
  if (console_svc_is_initialised()) {
    fail("uninit_is_initialised_true");
    return;
  }
  if (console_svc_port() != IPC_PORT_INVALID) {
    fail("uninit_port_not_invalid");
    return;
  }
  printf("TEST:PASS:console_svc_port_alloc_uninit\n");
}

static void test_init(void) {
  reset_world();
  console_svc_result_t rc = console_svc_init();
  if (rc != CONSOLE_SVC_OK) {
    fail("init_not_ok");
    return;
  }
  if (!console_svc_is_initialised()) {
    fail("init_flag_not_set");
    return;
  }
  ipc_port_t handle = console_svc_port();
  if (handle == IPC_PORT_INVALID) {
    fail("init_port_invalid");
    return;
  }

  cap_subject_id_t owner = 0u;
  if (ipc_port_owner(handle, &owner) != IPC_OK) {
    fail("init_owner_query_failed");
    return;
  }
  if (owner != (cap_subject_id_t)SUBJECT_M2_CONSOLE_SVC) {
    fail("init_owner_mismatch");
    return;
  }

  capability_id_t send_cap = 0;
  if (ipc_port_send_cap(handle, &send_cap) != IPC_OK) {
    fail("init_send_cap_query_failed");
    return;
  }
  if (send_cap != CAP_CONSOLE_WRITE) {
    fail("init_send_cap_mismatch");
    return;
  }

  capability_id_t recv_cap = 0;
  if (ipc_port_recv_cap(handle, &recv_cap) != IPC_OK) {
    fail("init_recv_cap_query_failed");
    return;
  }
  if (recv_cap != CAP_CONSOLE_WRITE) {
    fail("init_recv_cap_mismatch");
    return;
  }

  printf("TEST:PASS:console_svc_port_alloc_init\n");
}

static void test_double_init(void) {
  reset_world();
  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("double_first_init_failed");
    return;
  }
  ipc_port_t first = console_svc_port();
  if (first == IPC_PORT_INVALID) {
    fail("double_first_handle_invalid");
    return;
  }

  console_svc_result_t second = console_svc_init();
  if (second != CONSOLE_SVC_ERR_ALREADY_INIT) {
    fail("double_second_init_did_not_reject");
    return;
  }
  if (console_svc_port() != first) {
    fail("double_handle_changed_on_second_init");
    return;
  }
  printf("TEST:PASS:console_svc_port_alloc_double_init\n");
}

static void test_reset(void) {
  reset_world();
  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("reset_init_failed");
    return;
  }
  console_svc_reset();
  if (console_svc_is_initialised()) {
    fail("reset_flag_still_true");
    return;
  }
  if (console_svc_port() != IPC_PORT_INVALID) {
    fail("reset_port_not_invalid");
    return;
  }

  /* Fresh init after reset must succeed and produce a valid handle.
   * (We don't assert handle inequality vs. the pre-reset value: the
   * ipc_port_table_reset() above invalidates the prior generation, but
   * the handle's numeric value may legitimately repeat — the
   * staleness guarantee is the port table's job, not this module's.) */
  ipc_port_table_reset();
  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("reset_reinit_failed");
    return;
  }
  if (console_svc_port() == IPC_PORT_INVALID) {
    fail("reset_reinit_port_invalid");
    return;
  }
  printf("TEST:PASS:console_svc_port_alloc_reset\n");
}

int main(void) {
  test_uninit();
  test_init();
  test_double_init();
  test_reset();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:console_svc_port_alloc\n");
    return 1;
  }
  printf("TEST:PASS:console_svc_port_alloc\n");
  return 0;
}
