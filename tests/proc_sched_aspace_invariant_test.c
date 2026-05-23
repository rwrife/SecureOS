/**
 * @file proc_sched_aspace_invariant_test.c
 * @brief Regression: cooperative scheduler panics (via test hook) when
 *        asked to dispatch a PCB whose address_space_t fails the
 *        kernel-internal layout invariant.
 *
 * Issue: #260 done-when 3 ("scheduler block/wake panics on
 * saved-stack-out-of-window — covered by a host harness fixture that
 * exercises the check").
 *
 * Why a separate test binary:
 *   The existing proc_sched_test.c asserts the happy-path scheduler /
 *   IPC block/wake semantics. This binary deliberately corrupts a
 *   PCB's aspace and exercises the invariant-check path that aborts
 *   dispatch — keeping that fixture out of the happy-path harness
 *   keeps the two test surfaces orthogonal.
 *
 * What is asserted:
 *   1. A well-formed partitioned aspace dispatches normally and the
 *      panic hook is NEVER invoked (`TEST:PASS:proc_sched_aspace_invariant_clean`).
 *   2. A PCB whose aspace has `stack_top` outside the window triggers
 *      the panic hook exactly once with the expected reason string,
 *      and the scheduler force-exits the offending PCB so the
 *      dispatch loop terminates cleanly
 *      (`TEST:PASS:proc_sched_aspace_invariant_stack_escape`).
 *   3. A PCB whose aspace is NULL triggers the panic hook with
 *      `null_aspace` and is similarly force-exited
 *      (`TEST:PASS:proc_sched_aspace_invariant_null_aspace`).
 *   4. A PCB whose aspace has `ipc_scratch` straddling the window's
 *      upper boundary triggers `ipc_scratch_escapes_window`
 *      (`TEST:PASS:proc_sched_aspace_invariant_scratch_escape`).
 *
 * Launched by:
 *   build/scripts/test_proc_sched_aspace_invariant.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/proc/address_space.h"
#include "../kernel/proc/proc_sched.h"
#include "../kernel/proc/process.h"
#include "../user/include/secureos_abi.h"

static void die(const char *reason) {
  printf("TEST:FAIL:proc_sched_aspace_invariant:%s\n", reason);
  exit(1);
}

/* Panic-hook capture. The scheduler installs the hook before
 * dispatch; on a violation it records the offending PID + reason and
 * lets the scheduler force-exit the PCB so the test process does not
 * itself abort. */
static struct {
  int hits;
  process_id_t last_pid;
  char last_reason[64];
} g_panic;

static void panic_hook(process_id_t pid, const char *reason) {
  g_panic.hits++;
  g_panic.last_pid = pid;
  if (reason != NULL) {
    strncpy(g_panic.last_reason, reason, sizeof(g_panic.last_reason) - 1u);
    g_panic.last_reason[sizeof(g_panic.last_reason) - 1u] = '\0';
  } else {
    g_panic.last_reason[0] = '\0';
  }
}

static void reset_panic(void) {
  g_panic.hits = 0;
  g_panic.last_pid = PID_INVALID;
  g_panic.last_reason[0] = '\0';
}

static void reset_world(void) {
  proc_sched_reset();
  process_table_reset();
  cap_reset_for_tests();
  cap_table_reset();
  reset_panic();
  (void)proc_sched_set_panic_hook_for_tests(panic_hook);
}

/* Real arena so happy-path aspaces are well-formed. */
static uint8_t g_arena[4u * (PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64u)];
static address_space_t g_aspaces[4];

static void rebuild_arena(void) {
  if (aspace_partition((uintptr_t)g_arena, sizeof(g_arena),
                       g_aspaces, 4u) != ASPACE_OK) {
    die("partition_failed");
  }
}

/* A trivial scheduler entry that immediately exits; only reached on
 * the clean-path test. */
static int g_entry_runs = 0;
static void entry_exit_zero(void) {
  g_entry_runs++;
  (void)proc_exit(0u);
}

/* ------------------------------------------------------------------ */
/* (1) Clean path: well-formed partitioned aspace — no panic.          */
/* ------------------------------------------------------------------ */

static void test_clean_path(void) {
  reset_world();
  rebuild_arena();

  process_id_t pid = PID_INVALID;
  if (process_create(1u, &g_aspaces[0], &pid) != PROC_OK) die("create_clean");
  if (proc_sched_register(pid, entry_exit_zero) != PROC_SCHED_OK) {
    die("register_clean");
  }

  g_entry_runs = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_clean");
  if (g_panic.hits != 0) die("clean_path_panicked");
  if (g_entry_runs != 1) die("clean_entry_did_not_run");

  printf("TEST:PASS:proc_sched_aspace_invariant_clean\n");
}

/* ------------------------------------------------------------------ */
/* (2) stack_top escapes window.                                       */
/* ------------------------------------------------------------------ */

/* Should be unreachable: the scheduler panics before dispatching the
 * PCB whose context owns this entry. If it runs, the test fails
 * because the invariant gate was bypassed. */
static int g_bad_entry_ran = 0;
static void entry_should_not_run(void) {
  g_bad_entry_ran = 1;
  (void)proc_exit(0u);
}

static void test_stack_top_escapes(void) {
  reset_world();
  rebuild_arena();

  /* Corrupt one slot: push stack_top past the window's upper bound. */
  address_space_t corrupted = g_aspaces[0];
  corrupted.stack_top = corrupted.base + (uintptr_t)corrupted.size + 1u;

  process_id_t pid = PID_INVALID;
  if (process_create(1u, &corrupted, &pid) != PROC_OK) die("create_bad");
  if (proc_sched_register(pid, entry_should_not_run) != PROC_SCHED_OK) {
    die("register_bad");
  }

  g_bad_entry_ran = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_bad_should_drain");

  if (g_panic.hits != 1) die("expected_exactly_one_panic");
  if (g_panic.last_pid != pid) die("panic_pid_mismatch");
  if (strcmp(g_panic.last_reason, "stack_top_escapes_window") != 0) {
    printf("DEBUG:reason=%s\n", g_panic.last_reason);
    die("panic_reason_mismatch");
  }
  if (g_bad_entry_ran) die("invariant_gate_bypassed");

  /* PCB should have been force-exited with the sentinel exit code. */
  process_t snap;
  if (process_lookup(pid, &snap) != PROC_OK) die("lookup_after_panic");
  if (snap.state != PROC_STATE_EXITED) die("expected_exited_after_panic");
  if (snap.exit_code != UINT32_MAX) die("expected_sentinel_exit_code");

  printf("TEST:PASS:proc_sched_aspace_invariant_stack_escape\n");
}

/* ------------------------------------------------------------------ */
/* (3) NULL aspace.                                                    */
/* ------------------------------------------------------------------ */

static void test_null_aspace(void) {
  reset_world();
  rebuild_arena();

  process_id_t pid = PID_INVALID;
  if (process_create(1u, NULL, &pid) != PROC_OK) die("create_null");
  if (proc_sched_register(pid, entry_should_not_run) != PROC_SCHED_OK) {
    die("register_null");
  }

  g_bad_entry_ran = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_null_should_drain");

  if (g_panic.hits != 1) die("expected_one_panic_null");
  if (strcmp(g_panic.last_reason, "null_aspace") != 0) {
    printf("DEBUG:reason=%s\n", g_panic.last_reason);
    die("panic_reason_mismatch_null");
  }
  if (g_bad_entry_ran) die("null_invariant_gate_bypassed");

  printf("TEST:PASS:proc_sched_aspace_invariant_null_aspace\n");
}

/* ------------------------------------------------------------------ */
/* (4) ipc_scratch escapes window.                                     */
/* ------------------------------------------------------------------ */

static void test_scratch_escapes(void) {
  reset_world();
  rebuild_arena();

  /* Corrupt one slot: point ipc_scratch one byte before the window's
   * upper end so the IPC_MSG_PAYLOAD_MAX span straddles the boundary. */
  address_space_t corrupted = g_aspaces[0];
  uintptr_t end = corrupted.base + (uintptr_t)corrupted.size;
  corrupted.ipc_scratch = (uint8_t *)(end - 1u);

  process_id_t pid = PID_INVALID;
  if (process_create(1u, &corrupted, &pid) != PROC_OK) die("create_scratch");
  if (proc_sched_register(pid, entry_should_not_run) != PROC_SCHED_OK) {
    die("register_scratch");
  }

  g_bad_entry_ran = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_scratch_should_drain");

  if (g_panic.hits != 1) die("expected_one_panic_scratch");
  if (strcmp(g_panic.last_reason, "ipc_scratch_escapes_window") != 0) {
    printf("DEBUG:reason=%s\n", g_panic.last_reason);
    die("panic_reason_mismatch_scratch");
  }
  if (g_bad_entry_ran) die("scratch_invariant_gate_bypassed");

  printf("TEST:PASS:proc_sched_aspace_invariant_scratch_escape\n");
}

/* ------------------------------------------------------------------ */

int main(void) {
  printf("TEST:START:proc_sched_aspace_invariant\n");
  test_clean_path();
  test_stack_top_escapes();
  test_null_aspace();
  test_scratch_escapes();
  printf("TEST:PASS:proc_sched_aspace_invariant\n");
  return 0;
}
