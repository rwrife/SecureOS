/**
 * @file launcher_arena_bytes_test.c
 * @brief M7-TOOLCHAIN-001 slice 3 acceptance test (issue #448, refs
 *        #404 / #421 / #424).
 *
 * Asserts that `launcher_spawn_app_from_manifest()` (and its fs/broker
 * siblings) honors the optional `runtime.arena_bytes` manifest field
 * at spawn time per `docs/abi/manifest.md` §5.7:
 *
 *   - omitted (manifest->arena_bytes == 0) ⇒ resolved per-spawn cap
 *     equals `PROC_ARENA_BYTES_DEFAULT` (64 KiB) — byte-identical to
 *     the pre-#424 spawn / arena-sizing path.
 *   - declared value within `[DEFAULT, MAX]` ⇒ that value is the
 *     active cap returned by `launcher_arena_active_cap()`.
 *   - declared value > `PROC_ARENA_BYTES_MAX` (16 MiB) ⇒ spawn fails
 *     with `LAUNCHER_ERR_INVALID_MANIFEST`, the deny-audit cell records
 *     `LAUNCHER_ARENA_DENY_OVER_CAP` with the offending value, and the
 *     kernel does not panic (the test reaching its own next line is
 *     the "did not panic" signal — same shape as
 *     `tests/launcher_spawn_handoff_test.c`).
 *   - declared non-zero value < `PROC_ARENA_BYTES_DEFAULT` ⇒ same
 *     deny shape with reason `LAUNCHER_ARENA_DENY_UNDER_FLOOR`.
 *
 * Output markers (consumed by build/scripts/test_launcher_arena_bytes.sh):
 *   TEST:PASS:launcher_arena_bytes:default_when_omitted_matches_legacy
 *   TEST:PASS:launcher_arena_bytes:declared_value_applied
 *   TEST:PASS:launcher_arena_bytes:over_cap_denied
 *   TEST:PASS:launcher_arena_bytes:under_floor_denied
 *   TEST:PASS:launcher_arena_bytes
 *
 * Pure host-side; no kernel runtime dependencies beyond the launcher
 * spawn slice + its transitive M1 substrate. The sub-marker names
 * intentionally mirror those listed in the issue body so the marker
 * spelling is a single source of truth pinned across the doc / test /
 * script trio (same shape as the slice-2 handoff test).
 *
 * Issue: #448. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md §P1.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/proc/address_space.h"
#include "../kernel/proc/process.h"
#include "../kernel/user/launcher.h"
#include "harness/m2_subjects.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:launcher_arena_bytes:%s\n", reason);
  g_fail = 1;
}

static void test_setup(void) {
  /* Reset every shared singleton the launcher slice-3 path touches so
   * each sub-test starts from a known state. `launcher_reset()` calls
   * into `launcher_spawn_reset()` *and* `launcher_arena_audit_reset()`
   * so the deny cell + active-cap default are clean. */
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  launcher_spawn_reset();
  launcher_arena_audit_reset();
}

/* --------------------------------------------------------------
 * Sub-test 1: default_when_omitted_matches_legacy
 * -------------------------------------------------------------- */
static void test_default_when_omitted(void) {
  test_setup();

  /* arena_bytes == 0 is the legacy / back-compat path. We do not
   * assert the cap *is* PROC_ARENA_BYTES_DEFAULT directly via a magic
   * number — we use the launcher's own constant to remain in lock-
   * step with `address_space.h` if the default is ever bumped. */
  const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps = requested,
      .auto_grant_count = 1u,
      .arena_bytes = 0u,  /* omitted */
  };
  launcher_spawn_t spawn;
  memset(&spawn, 0, sizeof spawn);

  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_OK) {
    fail("default_spawn_rejected");
    return;
  }
  if (spawn.pid == PID_INVALID) {
    fail("default_spawn_pid_invalid");
    return;
  }
  if (launcher_arena_active_cap() != PROC_ARENA_BYTES_DEFAULT) {
    fail("default_cap_not_proc_arena_default");
    return;
  }
  /* Legacy-path proof: the deny audit cell MUST remain pristine
   * because no deny event was generated. */
  if (launcher_arena_last_deny_reason() != LAUNCHER_ARENA_DENY_NONE) {
    fail("default_path_emitted_deny");
    return;
  }
  if (launcher_arena_deny_count() != 0u) {
    fail("default_path_deny_count_nonzero");
    return;
  }

  (void)launcher_spawn_destroy(spawn.pid);
  printf("TEST:PASS:launcher_arena_bytes:default_when_omitted_matches_legacy\n");
}

/* --------------------------------------------------------------
 * Sub-test 2: declared_value_applied
 *
 * Picks 1 MiB — the representative mid-range value used by the
 * `helloapp.runtime_arena.json` example manifest the schema sub-slice
 * (#424) shipped — so the test pin matches the doc example exactly.
 * -------------------------------------------------------------- */
static void test_declared_value_applied(void) {
  test_setup();

  const uint32_t declared = 1u * 1024u * 1024u;  /* 1 MiB */
  const capability_id_t requested[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps = requested,
      .auto_grant_count = 1u,
      .arena_bytes = declared,
  };
  launcher_spawn_t spawn;
  memset(&spawn, 0, sizeof spawn);

  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_OK) {
    fail("declared_spawn_rejected");
    return;
  }
  if (launcher_arena_active_cap() != declared) {
    fail("declared_cap_not_propagated");
    return;
  }
  if (launcher_arena_last_deny_reason() != LAUNCHER_ARENA_DENY_NONE) {
    fail("declared_path_emitted_deny");
    return;
  }
  /* Boundary check: the inclusive upper bound MUST also succeed. */
  (void)launcher_spawn_destroy(spawn.pid);
  m.arena_bytes = PROC_ARENA_BYTES_MAX;
  memset(&spawn, 0, sizeof spawn);
  r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_OK) {
    fail("max_boundary_spawn_rejected");
    return;
  }
  if (launcher_arena_active_cap() != PROC_ARENA_BYTES_MAX) {
    fail("max_boundary_cap_mismatch");
    return;
  }
  (void)launcher_spawn_destroy(spawn.pid);

  /* Boundary check: the inclusive lower bound MUST also succeed. */
  m.arena_bytes = PROC_ARENA_BYTES_DEFAULT;
  memset(&spawn, 0, sizeof spawn);
  r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_OK) {
    fail("min_boundary_spawn_rejected");
    return;
  }
  if (launcher_arena_active_cap() != PROC_ARENA_BYTES_DEFAULT) {
    fail("min_boundary_cap_mismatch");
    return;
  }
  (void)launcher_spawn_destroy(spawn.pid);

  printf("TEST:PASS:launcher_arena_bytes:declared_value_applied\n");
}

/* --------------------------------------------------------------
 * Sub-test 3: over_cap_denied
 * -------------------------------------------------------------- */
static void test_over_cap_denied(void) {
  test_setup();

  const uint32_t requested_bytes = PROC_ARENA_BYTES_MAX + 1u;  /* >16 MiB */
  const capability_id_t caps[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps = caps,
      .auto_grant_count = 1u,
      .arena_bytes = requested_bytes,
  };
  launcher_spawn_t spawn;
  memset(&spawn, 0xAB, sizeof spawn);  /* poison to detect partial writes */

  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &spawn);
  /* Reaching this line at all is the "kernel did not panic" signal
   * — same shape as launcher_spawn_handoff's invalid-manifest leg. */
  if (r != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("over_cap_not_rejected");
    return;
  }
  if (spawn.pid != PID_INVALID) {
    /* The launcher zeroes out_spawn before any validation, so any
     * non-PID_INVALID here would mean a partial spawn leaked. */
    fail("over_cap_left_pid_live");
    return;
  }
  if (launcher_arena_last_deny_reason() != LAUNCHER_ARENA_DENY_OVER_CAP) {
    fail("over_cap_deny_reason_wrong");
    return;
  }
  if (launcher_arena_last_deny_value() != requested_bytes) {
    fail("over_cap_deny_value_wrong");
    return;
  }
  if (launcher_arena_deny_count() != 1u) {
    fail("over_cap_deny_count_wrong");
    return;
  }
  /* Try an even larger value: deny event must update, kernel must
   * survive. */
  m.arena_bytes = 0xFFFFFFFFu;
  r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("u32_max_not_rejected");
    return;
  }
  if (launcher_arena_deny_count() != 2u) {
    fail("over_cap_deny_count_not_incrementing");
    return;
  }

  printf("TEST:PASS:launcher_arena_bytes:over_cap_denied\n");
}

/* --------------------------------------------------------------
 * Sub-test 4: under_floor_denied
 * -------------------------------------------------------------- */
static void test_under_floor_denied(void) {
  test_setup();

  const uint32_t requested_bytes = PROC_ARENA_BYTES_DEFAULT - 1u;  /* <64 KiB */
  const capability_id_t caps[] = { CAP_CONSOLE_WRITE };
  launcher_manifest_t m = {
      .subject_id = SUBJECT_M2_HELLOAPP,
      .auto_grant_caps = caps,
      .auto_grant_count = 1u,
      .arena_bytes = requested_bytes,
  };
  launcher_spawn_t spawn;
  memset(&spawn, 0xCD, sizeof spawn);

  launcher_result_t r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("under_floor_not_rejected");
    return;
  }
  if (spawn.pid != PID_INVALID) {
    fail("under_floor_left_pid_live");
    return;
  }
  if (launcher_arena_last_deny_reason() != LAUNCHER_ARENA_DENY_UNDER_FLOOR) {
    fail("under_floor_deny_reason_wrong");
    return;
  }
  if (launcher_arena_last_deny_value() != requested_bytes) {
    fail("under_floor_deny_value_wrong");
    return;
  }
  /* Edge: exactly 1 byte is the smallest non-zero value and MUST be
   * denied (the floor is inclusive at 64 KiB). */
  m.arena_bytes = 1u;
  r = launcher_spawn_app_from_manifest(&m, &spawn);
  if (r != LAUNCHER_ERR_INVALID_MANIFEST) {
    fail("one_byte_not_rejected");
    return;
  }
  if (launcher_arena_last_deny_reason() != LAUNCHER_ARENA_DENY_UNDER_FLOOR) {
    fail("one_byte_deny_reason_wrong");
    return;
  }

  printf("TEST:PASS:launcher_arena_bytes:under_floor_denied\n");
}

int main(void) {
  printf("TEST:START:launcher_arena_bytes\n");

  test_default_when_omitted();
  test_declared_value_applied();
  test_over_cap_denied();
  test_under_floor_denied();

  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:launcher_arena_bytes\n");
  return 0;
}
