/**
 * @file proc_sched.h
 * @brief Cooperative process-level scheduler with IPC block/wake hooks
 *        (M1 plan #198 slice 3, issue #250).
 *
 * Purpose:
 *   The slice-1 PCB table (#224) only stored static state. The slice-2
 *   address-space carving (#248) only laid out per-process arenas.
 *   Slice 3 binds those PCBs to actual execution: each registered PCB
 *   gets a single entry function and a cooperative trampoline, the
 *   scheduler keeps a per-PCB execution context, and IPC send / recv
 *   can suspend a PCB on an empty/occupied IPC port slot and resume
 *   the matching peer when the rendezvous completes.
 *
 *   The v0 scheduler is deliberately small:
 *     - Single ready-queue (round-robin over READY PCBs).
 *     - Block-on-IPC is a recorded back-pointer (`blocked_on_port`)
 *       and a state transition READY -> BLOCKED. The scheduler refuses
 *       to dispatch BLOCKED PCBs; the IPC layer flips them back to
 *       READY when the peer rendezvous lands.
 *     - Cooperative yield only (no timer pre-emption — slice 4 / M2).
 *     - Single-threaded host model: at most one RUNNING PCB at a time.
 *
 *   Host-side context switching uses POSIX `ucontext.h` so the host
 *   tests can exercise true coroutine block/wake semantics without
 *   target-specific assembly. The kernel-side build path uses the
 *   same API: any freestanding port supplies its own ucontext shim
 *   (out of scope for #250; see #251 for the cap_id wiring follow-up).
 *
 * Interactions:
 *   - kernel/proc/process.{c,h}: owns PCB lifecycle and exposes the
 *     state / entry / exit_code / blocked_on_port mutator accessors
 *     this scheduler drives.
 *   - kernel/ipc/ipc_ops.c: calls proc_sched_block_current_on_port()
 *     when a recv finds an empty slot or a send finds an occupied slot,
 *     and proc_sched_wake_one_on_port() when the matching peer op
 *     completes the rendezvous.
 *   - tests/proc_sched_test.c (slice-3 acceptance harness).
 *
 * Launched by:
 *   Not a standalone process. The host-side test binary
 *   build/scripts/test_proc_sched.sh exercises the full scheduler +
 *   IPC block/wake path with deterministic TEST:PASS markers.
 *
 * Issue: #250. Plan: plans/2026-05-20-m1-process-address-space.md.
 */

#ifndef SECUREOS_KERNEL_PROC_SCHED_H
#define SECUREOS_KERNEL_PROC_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "process.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Scheduler-level result vocabulary. Distinct from proc_result_t so
 * callers can tell "the PCB-table refused this PID" (proc_result_t)
 * from "the scheduler refused this operation" (proc_sched_result_t).
 *
 * Frozen for OS_ABI_VERSION = 0: additive changes require an ABI bump
 * (#150). Tests assert exact numeric values for deterministic markers.
 */
typedef enum {
  PROC_SCHED_OK = 0,
  PROC_SCHED_ERR_INVALID_PID = 1,
  PROC_SCHED_ERR_INVALID_ARG = 2,
  PROC_SCHED_ERR_NO_RUNNABLE = 3,  /* no PCB in PROC_STATE_READY */
  PROC_SCHED_ERR_DEADLOCK = 4,     /* every live PCB is BLOCKED */
  PROC_SCHED_ERR_NOT_RUNNING = 5,  /* op requires a current PCB but none set */
  PROC_SCHED_ERR_NOT_BLOCKED = 6,  /* wake target was not BLOCKED on that port */
} proc_sched_result_t;

/*
 * Reset the scheduler to a known-empty state. Tears down every
 * coroutine context owned by previously-registered PCBs without
 * touching the underlying PCB table (that is process_table_reset's
 * job). Always call BOTH at the start of a test.
 */
void proc_sched_reset(void);

/*
 * Register a PCB with the scheduler. The PCB transitions
 * PROC_STATE_NEW -> PROC_STATE_READY and is appended to the
 * ready-queue rotation. `entry` MUST NOT be NULL.
 *
 * The entry runs at most once: on its first dispatch the scheduler
 * sets up a fresh ucontext stack, invokes entry(), and on return
 * implicitly performs proc_exit(0).
 *
 * Returns PROC_SCHED_ERR_INVALID_ARG on NULL entry, ERR_INVALID_PID
 * if the PCB is stale or already registered with this scheduler.
 */
proc_sched_result_t proc_sched_register(process_id_t pid, proc_entry_fn_t entry);

/*
 * Cooperative yield from the currently-running PCB. Saves the caller's
 * coroutine context and dispatches the next READY PCB in round-robin
 * order. Returns when the caller is next scheduled. Returns
 * PROC_SCHED_ERR_NOT_RUNNING if no PCB is currently running (i.e.,
 * called from the scheduler thread itself).
 */
proc_sched_result_t proc_yield(void);

/*
 * Terminate the currently-running PCB with `code`. Transitions its
 * state to PROC_STATE_EXITED, records `code`, and yields control to
 * the next READY PCB. This call NEVER returns to the caller; if no
 * PCB is currently running, returns PROC_SCHED_ERR_NOT_RUNNING.
 */
proc_sched_result_t proc_exit(uint32_t code);

/*
 * Run the scheduler until every registered PCB has exited or every
 * remaining live PCB is BLOCKED (deadlock). Returns PROC_SCHED_OK on
 * orderly drain, PROC_SCHED_ERR_DEADLOCK if at least one live PCB
 * remains BLOCKED with no possible waker.
 *
 * Single re-entrant guard: calling proc_sched_run() from inside a
 * scheduled PCB returns PROC_SCHED_ERR_INVALID_ARG.
 */
proc_sched_result_t proc_sched_run(void);

/*
 * Mark the currently-running PCB as BLOCKED on `port` (opaque
 * back-pointer; the scheduler only compares it for equality on wake)
 * and yield. Returns when the matching wake fires. Returns
 * PROC_SCHED_ERR_NOT_RUNNING if called outside a scheduled PCB and
 * PROC_SCHED_ERR_INVALID_ARG if `port` is NULL.
 *
 * Called by kernel/ipc/ipc_ops.c on the suspend path.
 */
proc_sched_result_t proc_sched_block_current_on_port(const void *port);

/*
 * Wake one PCB blocked on `port` (FIFO over the blocked list). The
 * woken PCB transitions BLOCKED -> READY and is re-appended to the
 * ready-queue tail. Returns PROC_SCHED_ERR_NOT_BLOCKED if no PCB is
 * blocked on that port.
 *
 * Called by kernel/ipc/ipc_ops.c when a rendezvous completes.
 */
proc_sched_result_t proc_sched_wake_one_on_port(const void *port);

/* ---------------- test-only inspectors (#250 acceptance) ---------- */

/*
 * Return the PID of the currently-running PCB, or PID_INVALID if no
 * PCB is running (i.e., called from the scheduler thread or before
 * proc_sched_run started dispatching).
 */
process_id_t proc_sched_current_for_tests(void);

/*
 * Count of PCBs currently in PROC_STATE_READY (ready-queue depth).
 */
uint32_t proc_sched_ready_count_for_tests(void);

/*
 * Count of PCBs currently in PROC_STATE_BLOCKED.
 */
uint32_t proc_sched_blocked_count_for_tests(void);

/*
 * Address-space invariant panic hook (issue #260 done-when 3).
 *
 * The scheduler verifies `aspace_invariant_ok(pcb->aspace)` immediately
 * before restoring a PCB's coroutine context. A `false` return means
 * the scheduler has been handed a corrupted window (NULL aspace, zero
 * size, base+size overflow, stack_top inversion/escape, or ipc_scratch
 * outside the window) — continuing would silently restore an
 * out-of-bounds kernel stack.
 *
 * In the freestanding kernel build this is a panic. The host-side
 * test build installs a hook so the regression test can observe the
 * violation deterministically: when a hook is installed, the
 * violation is reported via the hook (with the offending PID and a
 * short reason string) and the scheduler force-transitions the
 * offending PCB to PROC_STATE_EXITED with exit_code = UINT32_MAX so
 * the dispatch loop continues making forward progress instead of
 * hanging. When no hook is installed, the scheduler writes a
 * `PANIC:proc_sched:aspace_invariant:<pid>:<reason>` line to stderr
 * and calls abort() — matching the kernel-side semantics.
 *
 * Pure test-only mutator; not part of the kernel ABI. Always returns
 * the previously-installed hook so tests can chain / restore.
 */
typedef void (*proc_sched_panic_fn_t)(process_id_t pid, const char *reason);
proc_sched_panic_fn_t proc_sched_set_panic_hook_for_tests(
    proc_sched_panic_fn_t hook);

/*
 * Is a scheduler dispatch currently in progress? IPC ops use this to
 * pick between the v0 single-waiter-slot semantics (no scheduler
 * running -> return IPC_ERR_PEER_GONE) and the slice-3 block/wake
 * semantics (scheduler running -> suspend current PCB).
 */
bool proc_sched_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_SCHED_H */
