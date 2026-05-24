/**
 * @file fs_svc_port_alloc_test.c
 * @brief Validator for slice 1 of plan #277 (issue #278).
 *
 * Asserts the contract documented in `kernel/svc/fs_svc.h`:
 *
 *   1. Before init: fs_svc_is_initialised() is false and both
 *      fs_svc_port_read() / fs_svc_port_write() return
 *      IPC_PORT_INVALID.
 *   2. After init: fs_svc_is_initialised() is true; both handles are
 *      non-IPC_PORT_INVALID; the read handle is owned by
 *      SUBJECT_M3_FS_SVC with send_cap=recv_cap=CAP_FS_READ; the
 *      write handle is owned by SUBJECT_M3_FS_SVC with
 *      send_cap=recv_cap=CAP_FS_WRITE; the two handles are distinct.
 *   3. A second init without reset returns FS_SVC_ERR_ALREADY_INIT
 *      and does NOT allocate fresh ports (handles unchanged).
 *   4. After fs_svc_reset(): both ports are IPC_PORT_INVALID and
 *      is_initialised is false. A subsequent init succeeds and yields
 *      fresh handles.
 *
 * Output markers (consumed by build/scripts/test_fs_svc_port_alloc.sh):
 *   TEST:PASS:fs_svc_port_alloc_uninit
 *   TEST:PASS:fs_svc_port_alloc_init
 *   TEST:PASS:fs_svc_port_alloc_double_init
 *   TEST:PASS:fs_svc_port_alloc_reset
 *   TEST:PASS:fs_svc_port_alloc
 *
 * Pure host-side, no kernel runtime dependencies beyond the slice-1
 * source files. Modeled on tests/console_svc_port_alloc_test.c.
 *
 * Issue: #278. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 1.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/svc/fs_svc.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:fs_svc_port_alloc:%s\n", reason);
  g_fail = 1;
}

static void reset_world(void) {
  fs_svc_reset();
  ipc_port_table_reset();
}

static void test_uninit(void) {
  reset_world();
  if (fs_svc_is_initialised()) {
    fail("uninit_is_initialised_true");
    return;
  }
  if (fs_svc_port_read() != IPC_PORT_INVALID) {
    fail("uninit_read_port_not_invalid");
    return;
  }
  if (fs_svc_port_write() != IPC_PORT_INVALID) {
    fail("uninit_write_port_not_invalid");
    return;
  }
  printf("TEST:PASS:fs_svc_port_alloc_uninit\n");
}

static int assert_port(const char *label,
                       ipc_port_t handle,
                       capability_id_t expected_cap) {
  cap_subject_id_t owner = 0u;
  if (ipc_port_owner(handle, &owner) != IPC_OK) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_owner_query_failed", label);
    fail(r);
    return 0;
  }
  if (owner != (cap_subject_id_t)SUBJECT_M3_FS_SVC) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_owner_mismatch", label);
    fail(r);
    return 0;
  }
  capability_id_t send_cap = 0;
  if (ipc_port_send_cap(handle, &send_cap) != IPC_OK) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_send_cap_query_failed", label);
    fail(r);
    return 0;
  }
  if (send_cap != expected_cap) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_send_cap_mismatch", label);
    fail(r);
    return 0;
  }
  capability_id_t recv_cap = 0;
  if (ipc_port_recv_cap(handle, &recv_cap) != IPC_OK) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_recv_cap_query_failed", label);
    fail(r);
    return 0;
  }
  if (recv_cap != expected_cap) {
    char r[64];
    snprintf(r, sizeof(r), "init_%s_recv_cap_mismatch", label);
    fail(r);
    return 0;
  }
  return 1;
}

static void test_init(void) {
  reset_world();
  fs_svc_result_t rc = fs_svc_init();
  if (rc != FS_SVC_OK) {
    fail("init_not_ok");
    return;
  }
  if (!fs_svc_is_initialised()) {
    fail("init_flag_not_set");
    return;
  }
  ipc_port_t read_handle = fs_svc_port_read();
  if (read_handle == IPC_PORT_INVALID) {
    fail("init_read_port_invalid");
    return;
  }
  ipc_port_t write_handle = fs_svc_port_write();
  if (write_handle == IPC_PORT_INVALID) {
    fail("init_write_port_invalid");
    return;
  }
  if (read_handle == write_handle) {
    fail("init_handles_collide");
    return;
  }
  if (!assert_port("read", read_handle, CAP_FS_READ)) {
    return;
  }
  if (!assert_port("write", write_handle, CAP_FS_WRITE)) {
    return;
  }
  printf("TEST:PASS:fs_svc_port_alloc_init\n");
}

static void test_double_init(void) {
  reset_world();
  if (fs_svc_init() != FS_SVC_OK) {
    fail("double_first_init_failed");
    return;
  }
  ipc_port_t first_read = fs_svc_port_read();
  ipc_port_t first_write = fs_svc_port_write();
  if (first_read == IPC_PORT_INVALID || first_write == IPC_PORT_INVALID) {
    fail("double_first_handles_invalid");
    return;
  }

  fs_svc_result_t second = fs_svc_init();
  if (second != FS_SVC_ERR_ALREADY_INIT) {
    fail("double_second_init_did_not_reject");
    return;
  }
  if (fs_svc_port_read() != first_read) {
    fail("double_read_changed_on_second_init");
    return;
  }
  if (fs_svc_port_write() != first_write) {
    fail("double_write_changed_on_second_init");
    return;
  }
  printf("TEST:PASS:fs_svc_port_alloc_double_init\n");
}

static void test_reset(void) {
  reset_world();
  if (fs_svc_init() != FS_SVC_OK) {
    fail("reset_init_failed");
    return;
  }
  fs_svc_reset();
  if (fs_svc_is_initialised()) {
    fail("reset_flag_still_true");
    return;
  }
  if (fs_svc_port_read() != IPC_PORT_INVALID) {
    fail("reset_read_port_not_invalid");
    return;
  }
  if (fs_svc_port_write() != IPC_PORT_INVALID) {
    fail("reset_write_port_not_invalid");
    return;
  }

  ipc_port_table_reset();
  if (fs_svc_init() != FS_SVC_OK) {
    fail("reset_reinit_failed");
    return;
  }
  if (fs_svc_port_read() == IPC_PORT_INVALID) {
    fail("reset_reinit_read_port_invalid");
    return;
  }
  if (fs_svc_port_write() == IPC_PORT_INVALID) {
    fail("reset_reinit_write_port_invalid");
    return;
  }
  printf("TEST:PASS:fs_svc_port_alloc_reset\n");
}

int main(void) {
  test_uninit();
  test_init();
  test_double_init();
  test_reset();

  if (g_fail) {
    fprintf(stderr, "TEST:FAIL:fs_svc_port_alloc\n");
    return 1;
  }
  printf("TEST:PASS:fs_svc_port_alloc\n");
  return 0;
}
