/**
 * @file broker_svc_port_alloc_test.c
 * @brief Validator for slice 1 of plan #300 (issue #302).
 *
 * Asserts the contract documented in `kernel/svc/broker_svc.h`:
 *
 *   1. Before init: broker_svc_is_initialised() is false and
 *      broker_svc_port() returns IPC_PORT_INVALID.
 *   2. After init: broker_svc_is_initialised() is true; the handle
 *      is non-IPC_PORT_INVALID; the port is owned by
 *      SUBJECT_M4_BROKER_SVC with send_cap=recv_cap=CAP_IPC_SEND
 *      (option 1 per the plan — broker authority is subject-bound).
 *   3. A second init without reset returns BROKER_SVC_ERR_ALREADY_INIT
 *      and does NOT allocate a fresh port (handle unchanged).
 *   4. After broker_svc_reset(): the port is IPC_PORT_INVALID and
 *      is_initialised is false. A subsequent init succeeds and yields
 *      a fresh handle.
 *
 * Output markers (consumed by build/scripts/test_broker_svc_port_alloc.sh):
 *   TEST:PASS:broker_svc_port_alloc_uninit
 *   TEST:PASS:broker_svc_port_alloc_init
 *   TEST:PASS:broker_svc_port_alloc_double_init
 *   TEST:PASS:broker_svc_port_alloc_reset
 *   TEST:PASS:broker_svc_port_alloc
 *
 * Pure host-side, no kernel runtime dependencies beyond the slice-1
 * source files. Modeled on tests/fs_svc_port_alloc_test.c (#278) and
 * tests/console_svc_port_alloc_test.c (#272).
 *
 * Issue: #302. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md
 * slice 1.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/svc/broker_svc.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:broker_svc_port_alloc:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  broker_svc_reset();
  ipc_port_table_reset();
}

static void test_uninit(void) {
  reset_world();
  if (broker_svc_is_initialised()) {
    fail("uninit_is_initialised_true");
    return;
  }
  if (broker_svc_port() != IPC_PORT_INVALID) {
    fail("uninit_port_not_invalid");
    return;
  }
  printf("TEST:PASS:broker_svc_port_alloc_uninit\n");
}

static void test_init(void) {
  reset_world();
  broker_svc_result_t rc = broker_svc_init();
  if (rc != BROKER_SVC_OK) {
    fail("init_not_ok");
    return;
  }
  if (!broker_svc_is_initialised()) {
    fail("init_flag_not_set");
    return;
  }
  ipc_port_t handle = broker_svc_port();
  if (handle == IPC_PORT_INVALID) {
    fail("init_port_invalid");
    return;
  }
  cap_subject_id_t owner = 0u;
  if (ipc_port_owner(handle, &owner) != IPC_OK) {
    fail("init_owner_query_failed");
    return;
  }
  if (owner != (cap_subject_id_t)SUBJECT_M4_BROKER_SVC) {
    fail("init_owner_mismatch");
    return;
  }
  capability_id_t send_cap = 0;
  if (ipc_port_send_cap(handle, &send_cap) != IPC_OK) {
    fail("init_send_cap_query_failed");
    return;
  }
  if (send_cap != CAP_IPC_SEND) {
    fail("init_send_cap_mismatch");
    return;
  }
  capability_id_t recv_cap = 0;
  if (ipc_port_recv_cap(handle, &recv_cap) != IPC_OK) {
    fail("init_recv_cap_query_failed");
    return;
  }
  if (recv_cap != CAP_IPC_SEND) {
    fail("init_recv_cap_mismatch");
    return;
  }
  printf("TEST:PASS:broker_svc_port_alloc_init\n");
}

static void test_double_init(void) {
  reset_world();
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("double_first_init_failed");
    return;
  }
  ipc_port_t first = broker_svc_port();
  if (first == IPC_PORT_INVALID) {
    fail("double_first_handle_invalid");
    return;
  }
  broker_svc_result_t second = broker_svc_init();
  if (second != BROKER_SVC_ERR_ALREADY_INIT) {
    fail("double_second_init_did_not_reject");
    return;
  }
  if (broker_svc_port() != first) {
    fail("double_port_changed_on_second_init");
    return;
  }
  printf("TEST:PASS:broker_svc_port_alloc_double_init\n");
}

static void test_reset(void) {
  reset_world();
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("reset_init_failed");
    return;
  }
  broker_svc_reset();
  if (broker_svc_is_initialised()) {
    fail("reset_flag_still_true");
    return;
  }
  if (broker_svc_port() != IPC_PORT_INVALID) {
    fail("reset_port_not_invalid");
    return;
  }
  ipc_port_table_reset();
  if (broker_svc_init() != BROKER_SVC_OK) {
    fail("reset_reinit_failed");
    return;
  }
  if (broker_svc_port() == IPC_PORT_INVALID) {
    fail("reset_reinit_port_invalid");
    return;
  }
  printf("TEST:PASS:broker_svc_port_alloc_reset\n");
}

int main(void) {
  test_uninit();
  test_init();
  test_double_init();
  test_reset();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:broker_svc_port_alloc\n");
    return 1;
  }
  printf("TEST:PASS:broker_svc_port_alloc\n");
  return 0;
}
