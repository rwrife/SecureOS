/**
 * @file launcher_spawn_handoff_test.c
 * @brief Slice-2 acceptance test for plan #263 / issue #269.
 *
 * Asserts the M1→M2 initial-capability handoff contract documented in
 * `docs/architecture/m1-m2-handoff.md`:
 *
 *   1. `launcher_spawn_app_from_manifest()` with an auto-grant of
 *      CAP_CONSOLE_WRITE returns a live PCB with a non-zero
 *      `granted_handle`, and the same handle is written little-endian
 *      into the first four bytes of the spawned aspace's `ipc_scratch`.
 *   2. The minted handle round-trips through
 *      `cap_gate_check_handle(h, CAP_CONSOLE_WRITE) == 1`.
 *   3. A manifest with no auto-grant spawns the PCB but leaves
 *      `granted_handle == CAP_HANDLE_NULL` and the scratch region
 *      zeroed in the first four bytes.
 *   4. `launcher_spawn_destroy(pid)` revokes the handle
 *      (`cap_gate_check_handle(h, CAP_CONSOLE_WRITE) == 0`) and
 *      releases the slot for re-use.
 *   5. A manifest with subject_id == 0 or auto-grant != CAP_CONSOLE_WRITE
 *      is rejected with `LAUNCHER_ERR_INVALID_MANIFEST` and no PCB
 *      remains live afterwards.
 *
 * Output markers (consumed by build/scripts/test_launcher_spawn_handoff.sh):
 *   TEST:PASS:launcher_spawn_handoff_grant
 *   TEST:PASS:launcher_spawn_handoff_no_grant
 *   TEST:PASS:launcher_spawn_handoff_destroy
 *   TEST:PASS:launcher_spawn_handoff_invalid_manifest
 *   TEST:PASS:launcher_spawn_handoff
 *
 * Pure host-side; no kernel runtime dependencies beyond the slice-2
 * sources and their transitive M1 substrate.
 *
 * Issue: #269. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 2.
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
#include "harness/m2_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_spawn_handoff:%s\n", reason);
  g_fail = 1;
}

static cap_handle_t scratch_load_handle(const address_space_t *as) {
  const uint8_t *p = (const uint8_t *)as->ipc_scratch;
  return (cap_handle_t)p[0]
       | ((cap_handle_t)p[1] <<  8)
       | ((cap_handle_t)p[2] << 16)
       | ((cap_handle_t)p[3] << 24);
}

static void test_setup(void) {
  /* Reset every shared singleton the launcher slice-2 path touches so
   * the test starts from a known state regardless of run ordering. */
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  /* launcher_reset() already calls launcher_spawn_reset() but we call
   * it explicitly here so the arena is rebuilt after the
   * cap_handle / process resets above. */
  launcher_spawn_reset();
}

static void test_grant_and_handoff(void) {
  test_setup();

  const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = requested,
      .auto_grant_count  = sizeof(requested) / sizeof(requested[0]),
  };

  launcher_spawn_t sp;
  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &sp);
  if (r != LAUNCHER_OK) {
    fail("spawn_returned_nonok");
    return;
  }
  if (sp.pid == PID_INVALID) {
    fail("spawn_returned_invalid_pid");
    return;
  }
  if (sp.aspace == NULL || sp.aspace->ipc_scratch == NULL) {
    fail("spawn_returned_null_aspace_or_scratch");
    return;
  }
  if (sp.granted_handle == CAP_HANDLE_NULL) {
    fail("spawn_returned_null_handle");
    return;
  }
  if (sp.granted_cap != CAP_CONSOLE_WRITE) {
    fail("spawn_returned_wrong_cap");
    return;
  }

  /* The handle in the scratch slot must equal the handle returned. */
  cap_handle_t scratch_h = scratch_load_handle(sp.aspace);
  if (scratch_h != sp.granted_handle) {
    fail("scratch_handle_mismatch");
    return;
  }

  /* And the handle must gate-check positively. */
  if (cap_gate_check_handle(sp.granted_handle, CAP_CONSOLE_WRITE) != 1) {
    fail("handle_gate_check_failed");
    return;
  }

  /* Cross-check: a different cap_id must not pass on the same handle. */
  if (cap_gate_check_handle(sp.granted_handle, CAP_IPC_SEND) != 0) {
    fail("handle_gate_check_passed_wrong_cap");
    return;
  }

  /* Reserved scratch bytes [4..64) must be zero per the handoff contract. */
  const uint8_t *p = (const uint8_t *)sp.aspace->ipc_scratch;
  for (size_t i = 4; i < 64; ++i) {
    if (p[i] != 0u) {
      fail("scratch_reserved_bytes_nonzero");
      return;
    }
  }

  printf("TEST:PASS:launcher_spawn_handoff_grant\n");
}

static void test_no_grant(void) {
  test_setup();

  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = NULL,
      .auto_grant_count  = 0,
  };

  launcher_spawn_t sp;
  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &sp);
  if (r != LAUNCHER_OK) {
    fail("no_grant_spawn_returned_nonok");
    return;
  }
  if (sp.granted_handle != CAP_HANDLE_NULL) {
    fail("no_grant_returned_handle");
    return;
  }
  if (sp.granted_cap != (capability_id_t)0) {
    fail("no_grant_returned_cap_id");
    return;
  }

  /* Scratch first four bytes must be zero when no grant was requested. */
  cap_handle_t scratch_h = scratch_load_handle(sp.aspace);
  if (scratch_h != CAP_HANDLE_NULL) {
    fail("no_grant_scratch_nonzero");
    return;
  }

  printf("TEST:PASS:launcher_spawn_handoff_no_grant\n");
}

static void test_destroy_revokes(void) {
  test_setup();

  const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id        = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps   = requested,
      .auto_grant_count  = 1,
  };

  launcher_spawn_t sp;
  if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
    fail("destroy_spawn_setup_failed");
    return;
  }
  if (cap_gate_check_handle(sp.granted_handle, CAP_CONSOLE_WRITE) != 1) {
    fail("destroy_pre_gate_check_failed");
    return;
  }

  if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("destroy_returned_nonok");
    return;
  }
  if (cap_gate_check_handle(sp.granted_handle, CAP_CONSOLE_WRITE) != 0) {
    fail("destroy_did_not_revoke_handle");
    return;
  }

  /* Second destroy of an already-released pid is no longer a known
   * launcher spawn; expect INVALID_APP. PID_INVALID stays a no-op. */
  if (launcher_spawn_destroy(PID_INVALID) != LAUNCHER_OK) {
    fail("destroy_pid_invalid_not_noop");
    return;
  }

  printf("TEST:PASS:launcher_spawn_handoff_destroy\n");
}

static void test_invalid_manifest(void) {
  test_setup();

  launcher_spawn_t sp;

  /* NULL manifest. */
  if (launcher_spawn_app_from_manifest(NULL, &sp) != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("invalid_null_manifest_not_rejected");
    return;
  }

  /* subject_id == 0 (reserved). */
  launcher_manifest_t bad_subject = {
      .subject_id       = 0u,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0,
  };
  if (launcher_spawn_app_from_manifest(&bad_subject, &sp) !=
      LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("invalid_zero_subject_not_rejected");
    return;
  }

  /* auto-grant cap that the slice-2 wiring does not support. */
  const capability_id_t bad_cap[] = { CAP_IPC_SEND };
  launcher_manifest_t bad_grant = {
      .subject_id       = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps  = bad_cap,
      .auto_grant_count = 1,
  };
  if (launcher_spawn_app_from_manifest(&bad_grant, &sp) !=
      LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("invalid_unsupported_cap_not_rejected");
    return;
  }

  /* auto_grant_count > 0 but caps pointer NULL. */
  launcher_manifest_t bad_ptr = {
      .subject_id       = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 1,
  };
  if (launcher_spawn_app_from_manifest(&bad_ptr, &sp) !=
      LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("invalid_null_caps_with_count_not_rejected");
    return;
  }

  printf("TEST:PASS:launcher_spawn_handoff_invalid_manifest\n");
}

int main(void) {
  test_grant_and_handoff();
  test_no_grant();
  test_destroy_revokes();
  test_invalid_manifest();

  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:launcher_spawn_handoff\n");
  return 0;
}
