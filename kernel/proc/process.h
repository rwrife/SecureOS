/**
 * @file process.h
 * @brief M1 process table — PCB struct + scheduler-visible PID lookup
 *        (issue #224, plan plans/2026-05-20-m1-process-address-space.md).
 *
 * Purpose:
 *   First execute slice of the M1 process abstraction. Defines the
 *   minimal Process Control Block (PCB) shape and a fixed-size process
 *   table indexed by a stable `process_id_t`. Each PCB carries exactly
 *   one `cap_subject_id_t` so the IPC primitive (#220) and the
 *   capability gate (#225 / #237) can address a process by its identity
 *   without any further indirection.
 *
 *   The handle representation mirrors the lifecycle pattern already
 *   established by `kernel/ipc/ipc_port.{c,h}` (#220) and
 *   `kernel/cap/cap_handle.{c,h}` (#237): the low 16 bits are the table
 *   index and the high 16 bits are a generation counter that advances
 *   on every `process_destroy` so stale PIDs fail cleanly with
 *   `PROC_ERR_INVALID_PID` rather than aliasing a recycled slot.
 *
 *   What this slice DOES NOT do (explicit non-asks from the issue —
 *   each is its own follow-up):
 *     - No scheduling logic, no ready-queue, no context switch.
 *     - No concrete `address_space_t` fields (paging issue, M2+).
 *     - No syscall entry wiring (#232 / `syscall_entry.{c,h}` owns it).
 *     - No IPC integration (#220's port table already owns its own
 *       lifecycle; this table is referenced by name only).
 *
 *   `address_space_t` is declared opaque here. v0 only requires that a
 *   PCB carry an `address_space_t *` slot — its concrete layout is
 *   reserved for the M2 paging slice. `process_create` accepts a
 *   caller-supplied pointer (typically NULL in v0 tests) and stores it
 *   verbatim; no allocation, no validation beyond NULL-vs-non-NULL.
 *
 * Interactions:
 *   - kernel/cap/capability.h: `cap_subject_id_t` is the identity
 *     bound to each PCB.
 *   - kernel/ipc/ipc_port.{c,h}: same generation-counter pattern (#220).
 *   - user/include/secureos_abi.h: `OS_ABI_VERSION = 0` anchors the
 *     packed handle layout via static_assert in process.c.
 *
 * Launched by:
 *   Not a standalone process. `process_table_init()` is called by
 *   tests at start-of-run and (when the kernel boots) from kmain at
 *   subsystem init time. Currently no production caller — wiring
 *   into `kernel_main` is deferred to the cooperative-scheduler
 *   follow-up issue.
 *
 * Issue: #224. Plan: plans/2026-05-20-m1-process-address-space.md.
 */

#ifndef SECUREOS_KERNEL_PROC_PROCESS_H
#define SECUREOS_KERNEL_PROC_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum simultaneously live processes in the v0 in-kernel scaffold.
 * Sized deliberately small (matches IPC_PORT_TABLE_MAX from #220) —
 * anything that needs more is out of scope for the session-sized M1
 * slice. Raising this is allowed at M2+ without ABI churn because the
 * value is not exposed across the ABI boundary.
 */
#define PROC_TABLE_MAX 16u

/*
 * Reserved invalid PID. `process_create` never returns this value on
 * success. Tests use it to assert default-zero PIDs are rejected on
 * every lookup / destroy.
 */
#define PID_INVALID 0u

typedef uint32_t process_id_t;
typedef process_id_t pid_t_proc; /* internal alias to avoid clashing
                                    with POSIX pid_t in host tests */

/*
 * Result vocabulary for the process-table API. Kept distinct from
 * cap_result_t / ipc_result_t so callers cannot accidentally cross
 * subsystem boundaries with a single switch.
 */
typedef enum {
  PROC_OK = 0,
  PROC_ERR_INVALID_PID = 1,
  PROC_ERR_INVALID_ARG = 2,
  PROC_ERR_TABLE_FULL = 3,
} proc_result_t;

/*
 * Cooperative-scheduler state machine (slice 3, issue #250 / plan #198).
 * The values are part of the v0 in-kernel contract: tests rely on the
 * specific enumeration order (NEW < READY < RUNNING < BLOCKED < EXITED)
 * to assert lifecycle transitions. Do not reorder.
 */
typedef enum {
  PROC_STATE_NEW = 0,
  PROC_STATE_READY = 1,
  PROC_STATE_RUNNING = 2,
  PROC_STATE_BLOCKED = 3,
  PROC_STATE_EXITED = 4,
} process_state_t;

/*
 * Slice-3 entry function shape. The cooperative scheduler invokes a
 * registered entry once when the PCB first runs. Entries may call
 * proc_yield() (and ipc_send/recv as their only blocking points) and
 * are expected to call proc_exit() before returning.
 */
typedef void (*proc_entry_fn_t)(void);

/*
 * Forward declaration of the address-space placeholder. The v0
 * scaffold treats `address_space_t` as an opaque type: concrete fields
 * are reserved for the M2 paging slice and live behind this pointer
 * inside the PCB. Declaring it here (rather than in a separate
 * address_space.h) keeps the v0 surface to a single header.
 */
typedef struct address_space address_space_t;

/*
 * Minimal PCB. Field order is part of the v0 contract — tests assert
 * sizeof/offsetof against a static_assert table in process.c.
 *
 * Slice 3 (#250) appends cooperative-scheduler bookkeeping at the tail
 * (state, entry, exit_code, blocked_on_port) so the existing
 * sizeof/offsetof contract from slice 1 (pid/subject/aspace at the
 * front) is preserved. process_lookup() still only fills the first
 * three fields plus the new state/exit_code so callers that snapshot
 * a PCB never see uninitialised tail bytes.
 */
typedef struct process {
  process_id_t       pid;            /* stable handle, 0 = invalid */
  cap_subject_id_t   subject;        /* capability identity bound to this PCB */
  address_space_t   *aspace;         /* opaque in v0; reserved for M2 paging */
  /* --- slice 3 (#250): cooperative-scheduler tail, append-only --- */
  process_state_t    state;          /* scheduler lifecycle state */
  proc_entry_fn_t    entry;          /* registered entry; NULL pre-spawn */
  uint32_t           exit_code;      /* valid only in PROC_STATE_EXITED */
  const void        *blocked_on_port;/* opaque IPC port back-ref or NULL */
} process_t;

/*
 * Initialize / reset the process table to a known-empty state. Both
 * forms are idempotent. Calling reset invalidates every previously
 * issued PID (their generations are bumped by the reset).
 */
void process_table_init(void);
void process_table_reset(void);

/*
 * Create a process bound to `subject`. `aspace` is stored verbatim in
 * the PCB and may be NULL in v0. Returns PROC_OK and writes the new
 * PID to `*out_pid` on success; PROC_ERR_INVALID_ARG if `out_pid` is
 * NULL; PROC_ERR_TABLE_FULL when no slot is free.
 *
 * The returned PID is guaranteed never to equal PID_INVALID.
 */
proc_result_t process_create(cap_subject_id_t subject,
                             address_space_t *aspace,
                             process_id_t *out_pid);

/*
 * Destroy a process. Subsequent operations on the same PID return
 * PROC_ERR_INVALID_PID (generation mismatch). Double-destroy is also
 * PROC_ERR_INVALID_PID; destroying PID_INVALID is PROC_ERR_INVALID_PID.
 */
proc_result_t process_destroy(process_id_t pid);

/*
 * Look up a live PCB. On success writes a snapshot of the PCB fields
 * into `*out_proc` and returns PROC_OK. On a stale or invalid PID
 * returns PROC_ERR_INVALID_PID and leaves `*out_proc` untouched.
 *
 * The snapshot is a value copy — callers MUST NOT cache the pointer
 * across other process_table_* calls. v0 intentionally does not
 * expose live PCB pointers across the API boundary.
 */
proc_result_t process_lookup(process_id_t pid, process_t *out_proc);

/*
 * Test-only helper: is `pid` currently live in the table?
 * Used by tests/process_table_test.c to assert table-occupancy
 * transitions without peeking at slot internals.
 */
bool process_is_live_for_tests(process_id_t pid);

/* ----------------------------------------------------------------
 * Slice-3 (#250) mutator accessors used by kernel/proc/proc_sched.{c,h}
 * to drive the cooperative-scheduler state machine without exposing
 * the raw slot struct. All four return PROC_ERR_INVALID_PID on a stale
 * or invalid handle and PROC_OK on success.
 *
 * These are intentionally NOT part of the public PCB-table API for
 * non-scheduler callers — the only in-kernel callers (besides the
 * proc_sched test) are the cooperative scheduler itself and the IPC
 * send/recv block/wake path in kernel/ipc/ipc_ops.c.
 * ---------------------------------------------------------------- */
proc_result_t process_set_state(process_id_t pid, process_state_t state);
proc_result_t process_set_entry(process_id_t pid, proc_entry_fn_t entry);
proc_result_t process_set_exit_code(process_id_t pid, uint32_t code);
proc_result_t process_set_blocked_on(process_id_t pid, const void *port);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_PROCESS_H */
