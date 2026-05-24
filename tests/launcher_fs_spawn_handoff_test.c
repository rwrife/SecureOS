/**
 * @file launcher_fs_spawn_handoff_test.c
 * @brief Slice-2 acceptance test for plan #277 / issue #279
 *        (M3-SUBSTRATE-002).
 *
 * Asserts the M3 launcher-fs spawn handoff contract:
 *
 *   1. `launcher_fs_spawn_app_with_fs_caps(..., grant_write=false, ...)`
 *      returns a live PCB with a non-zero CAP_FS_READ handle stamped
 *      LE64 into `ipc_scratch[8..16)`, and `ipc_scratch[16..24)` is
 *      CAP_HANDLE_NULL.
 *   2. `launcher_fs_spawn_app_with_fs_caps(..., grant_write=true, ...)`
 *      stamps both handles; both resolve via `cap_handle_owner()` to
 *      the spawned subject; both pass `cap_gate_check_handle()` for
 *      their respective caps.
 *   3. `launcher_fs_spawn_destroy(pid)` invalidates BOTH handles via
 *      the cap_handle_revoke_subject() cascade in `process_destroy()`.
 *
 * Output markers (consumed by build/scripts/test_launcher_fs_spawn_handoff.sh):
 *   TEST:PASS:launcher_fs_spawn_handoff_read_only
 *   TEST:PASS:launcher_fs_spawn_handoff_read_write
 *   TEST:PASS:launcher_fs_spawn_handoff_revoke_on_destroy
 *   TEST:PASS:launcher_fs_spawn_handoff
 *
 * Pure host-side; no kernel runtime dependencies beyond the slice-2
 * sources and their transitive M1 substrate.
 *
 * Issue: #279. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 2.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/proc/process.h"
#include "../kernel/user/launcher.h"
#include "../kernel/user/launcher_fs.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_fs_spawn_handoff:%s\n", reason);
  g_fail = 1;
}

/* Read 8 little-endian bytes; the low 32 bits are cap_handle_t under
 * OS_ABI_VERSION=0. Top 32 bits are reserved-zero. */
static cap_handle_t scratch_load_handle_le64(const uint8_t *p) {
  cap_handle_t lo = (cap_handle_t)p[0]
                  | ((cap_handle_t)p[1] <<  8)
                  | ((cap_handle_t)p[2] << 16)
                  | ((cap_handle_t)p[3] << 24);
  return lo;
}

static int scratch_top32_zero(const uint8_t *p) {
  return p[4] == 0u && p[5] == 0u && p[6] == 0u && p[7] == 0u;
}

static void test_setup(void) {
  launcher_reset();
  launcher_fs_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
}

static void test_read_only(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_fs_spawn_t sp;
  launcher_result_t r = launcher_fs_spawn_app_with_fs_caps(&m, 0, &sp);
  if (r != LAUNCHER_OK) {
    fail("read_only_spawn_returned_nonok");
    return;
  }
  if (sp.pid == PID_INVALID) {
    fail("read_only_invalid_pid");
    return;
  }
  if (sp.aspace == NULL || sp.aspace->ipc_scratch == NULL) {
    fail("read_only_null_aspace_or_scratch");
    return;
  }
  if (sp.read_handle == CAP_HANDLE_NULL) {
    fail("read_only_null_read_handle");
    return;
  }
  if (sp.write_handle != CAP_HANDLE_NULL) {
    fail("read_only_nonzero_write_handle");
    return;
  }

  const uint8_t *p = (const uint8_t *)sp.aspace->ipc_scratch;

  /* Console reserved slot [0..8) must be zero on an fs-cap spawn. */
  for (size_t i = 0; i < 8; ++i) {
    if (p[i] != 0u) {
      fail("read_only_console_slot_nonzero");
      return;
    }
  }

  /* [8..16): LE64(read_handle). */
  if (scratch_load_handle_le64(&p[8]) != sp.read_handle) {
    fail("read_only_scratch_read_handle_mismatch");
    return;
  }
  if (!scratch_top32_zero(&p[8])) {
    fail("read_only_scratch_read_top32_nonzero");
    return;
  }

  /* [16..24): LE64(CAP_HANDLE_NULL). */
  if (scratch_load_handle_le64(&p[16]) != CAP_HANDLE_NULL) {
    fail("read_only_scratch_write_slot_nonzero");
    return;
  }
  if (!scratch_top32_zero(&p[16])) {
    fail("read_only_scratch_write_top32_nonzero");
    return;
  }

  /* Reserved scratch bytes [24..64) must be zero. */
  for (size_t i = 24; i < 64; ++i) {
    if (p[i] != 0u) {
      fail("read_only_scratch_reserved_bytes_nonzero");
      return;
    }
  }

  /* Read handle must gate-check positively for CAP_FS_READ and
   * negatively for CAP_FS_WRITE. */
  if (cap_gate_check_handle(sp.read_handle, CAP_FS_READ) != 1) {
    fail("read_only_read_handle_gate_check_failed");
    return;
  }
  if (cap_gate_check_handle(sp.read_handle, CAP_FS_WRITE) != 0) {
    fail("read_only_read_handle_passed_write");
    return;
  }
  if (cap_handle_owner(sp.read_handle) != SUBJECT_M2_HELLOAPP) {
    fail("read_only_read_handle_owner_mismatch");
    return;
  }

  printf("TEST:PASS:launcher_fs_spawn_handoff_read_only\n");
}

static void test_read_write(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_fs_spawn_t sp;
  launcher_result_t r = launcher_fs_spawn_app_with_fs_caps(&m, 1, &sp);
  if (r != LAUNCHER_OK) {
    fail("read_write_spawn_returned_nonok");
    return;
  }
  if (sp.read_handle == CAP_HANDLE_NULL || sp.write_handle == CAP_HANDLE_NULL) {
    fail("read_write_null_handle");
    return;
  }
  if (sp.read_handle == sp.write_handle) {
    fail("read_write_handles_aliased");
    return;
  }

  const uint8_t *p = (const uint8_t *)sp.aspace->ipc_scratch;
  if (scratch_load_handle_le64(&p[8])  != sp.read_handle ||
      scratch_load_handle_le64(&p[16]) != sp.write_handle) {
    fail("read_write_scratch_handle_mismatch");
    return;
  }
  if (!scratch_top32_zero(&p[8]) || !scratch_top32_zero(&p[16])) {
    fail("read_write_scratch_top32_nonzero");
    return;
  }

  if (cap_gate_check_handle(sp.read_handle,  CAP_FS_READ)  != 1 ||
      cap_gate_check_handle(sp.write_handle, CAP_FS_WRITE) != 1) {
    fail("read_write_gate_check_failed");
    return;
  }
  if (cap_handle_owner(sp.read_handle)  != SUBJECT_M2_HELLOAPP ||
      cap_handle_owner(sp.write_handle) != SUBJECT_M2_HELLOAPP) {
    fail("read_write_owner_mismatch");
    return;
  }

  /* Cross-check: write handle must NOT gate as CAP_FS_READ. */
  if (cap_gate_check_handle(sp.write_handle, CAP_FS_READ) != 0) {
    fail("read_write_write_handle_passed_read");
    return;
  }

  printf("TEST:PASS:launcher_fs_spawn_handoff_read_write\n");
}

static void test_revoke_on_destroy(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_fs_spawn_t sp;
  if (launcher_fs_spawn_app_with_fs_caps(&m, 1, &sp) != LAUNCHER_OK) {
    fail("revoke_setup_failed");
    return;
  }
  if (cap_gate_check_handle(sp.read_handle,  CAP_FS_READ)  != 1 ||
      cap_gate_check_handle(sp.write_handle, CAP_FS_WRITE) != 1) {
    fail("revoke_pre_gate_check_failed");
    return;
  }

  if (launcher_fs_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("revoke_destroy_returned_nonok");
    return;
  }
  if (cap_gate_check_handle(sp.read_handle,  CAP_FS_READ)  != 0) {
    fail("revoke_read_handle_still_live");
    return;
  }
  if (cap_gate_check_handle(sp.write_handle, CAP_FS_WRITE) != 0) {
    fail("revoke_write_handle_still_live");
    return;
  }

  /* PID_INVALID stays a no-op. */
  if (launcher_fs_spawn_destroy(PID_INVALID) != LAUNCHER_OK) {
    fail("revoke_pid_invalid_not_noop");
    return;
  }

  printf("TEST:PASS:launcher_fs_spawn_handoff_revoke_on_destroy\n");
}

int main(void) {
  test_read_only();
  test_read_write();
  test_revoke_on_destroy();

  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:launcher_fs_spawn_handoff\n");
  return 0;
}
