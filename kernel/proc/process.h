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
 * Out of scope for this slice (these will appear in sibling follow-up
 * issues; do NOT add them here): state machine, ready-queue links,
 * exit_code, entry pointer, kernel stack pointer.
 */
typedef struct process {
  process_id_t       pid;      /* stable handle, 0 = invalid */
  cap_subject_id_t   subject;  /* capability identity bound to this PCB */
  address_space_t   *aspace;   /* opaque in v0; reserved for M2 paging */
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

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_PROCESS_H */
