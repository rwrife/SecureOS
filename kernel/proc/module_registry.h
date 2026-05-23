/**
 * @file module_registry.h
 * @brief M1 acceptance-demo module registry (issue #251, plan #198 slice 4).
 *
 * Purpose:
 *   Static, compile-time table of the three "modules" used by the M1
 *   two-module IPC acceptance demo (BUILD_ROADMAP §5.1):
 *
 *     name           subject                 declared cap        role
 *     m1-sender      SUBJECT_M1_SENDER       CAP_IPC_SEND        allow-path send
 *     m1-receiver    SUBJECT_M1_RECEIVER     CAP_IPC_RECV        port owner / recv
 *     m1-unauth      SUBJECT_M1_UNAUTH       (none)              deny-path send
 *
 *   `proc_spawn_module(name, out_pid)` looks up the entry by name,
 *   creates a PCB bound to the registered subject, grants the declared
 *   capability via BOTH `cap_table_grant` (for the audit-ring path the
 *   handle-gated IPC ops still drive) and `cap_handle_grant` (for the
 *   authoritative handle-keyed decision), and registers a kernel-stack
 *   entry stub with the cooperative scheduler.
 *
 *   The IPC port used by the demo is owned by the test harness and
 *   announced to the registry via `m1_demo_set_port` before any module
 *   is spawned. The registry's per-module entry stubs route through
 *   `ipc_send_h` / `ipc_recv_h` using the handle issued at spawn time
 *   (looked up by subject id via `m1_demo_get_handle_for`).
 *
 * Out of scope (per issue #251):
 *   - No new syscall surface, no ABI bump.
 *   - No filesystem-backed module load (M3).
 *   - No dynamic grants — the cap set per module is fixed at compile
 *     time so the demo is deterministic.
 *
 * Interactions:
 *   - kernel/proc/process.{c,h}: process_create + PCB lifecycle.
 *   - kernel/proc/proc_sched.{c,h}: proc_sched_register.
 *   - kernel/cap/cap_table.h:  cap_table_grant for audit-ring parity.
 *   - kernel/cap/cap_handle.h: cap_handle_grant for the handle-gated
 *     IPC decision.
 *   - kernel/ipc/ipc_ops.h:    ipc_send_h / ipc_recv_h.
 *
 * Launched by:
 *   tests/m1_ipc_demo_test.c (host build), dispatched via
 *   build/scripts/test_m1_ipc_demo.sh.
 *
 * Issue: #251. Plan: plans/2026-05-20-m1-process-address-space.md slice 4.
 */

#ifndef SECUREOS_KERNEL_PROC_MODULE_REGISTRY_H
#define SECUREOS_KERNEL_PROC_MODULE_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../cap/cap_handle.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"
#include "process.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stable subject ids for the three M1 demo modules. Frozen under
 * OS_ABI_VERSION = 0 — tests pin the exact values so a regression in
 * the registry is caught immediately.
 */
/*
 * NOTE: subject ids are kept under CAP_TABLE_MAX_SUBJECTS (8 in v0)
 * because the audit-ring parity hit in ipc_send_h / ipc_recv_h still
 * threads through cap_table_check, which rejects out-of-range subject
 * ids with CAP_ERR_SUBJECT_INVALID. Growing the bitset table is
 * tracked separately under the M1-CAPTBL umbrella.
 */
#define SUBJECT_M1_SENDER   ((cap_subject_id_t)5u)
#define SUBJECT_M1_RECEIVER ((cap_subject_id_t)6u)
#define SUBJECT_M1_UNAUTH   ((cap_subject_id_t)7u)

/*
 * Result vocabulary for proc_spawn_module. Distinct from proc_result_t
 * and proc_sched_result_t so callers can tell registry-level failures
 * (unknown name, demo state not initialised) from PCB-table or
 * scheduler-level failures.
 */
typedef enum {
  MODULE_SPAWN_OK = 0,
  MODULE_SPAWN_ERR_UNKNOWN_NAME = 1,
  MODULE_SPAWN_ERR_INVALID_ARG = 2,
  MODULE_SPAWN_ERR_PROC_CREATE = 3,
  MODULE_SPAWN_ERR_SCHED_REGISTER = 4,
  MODULE_SPAWN_ERR_CAP_GRANT = 5,
  MODULE_SPAWN_ERR_DEMO_UNINITIALISED = 6,
} module_spawn_result_t;

/*
 * Reset the demo's transient state (the announced port and the
 * per-subject handle map). Always call at the start of a test phase
 * alongside process_table_reset / proc_sched_reset / cap_*_reset.
 */
void m1_demo_reset(void);

/*
 * Announce the IPC port the demo will exchange over. The port MUST be
 * owned by SUBJECT_M1_RECEIVER and configured with send_cap=CAP_IPC_SEND
 * / recv_cap=CAP_IPC_RECV; the registry does not enforce this beyond
 * storing the handle, but the demo entry stubs assume it.
 *
 * Returns false if `port` equals IPC_PORT_INVALID; otherwise true.
 */
bool m1_demo_set_port(ipc_port_t port);

/*
 * Look up the cap_handle_t most recently issued to `subject` by a
 * spawn call. Returns CAP_HANDLE_NULL if no spawn issued one (e.g.
 * SUBJECT_M1_UNAUTH — its registered cap set is empty so no handle is
 * granted at spawn time).
 */
cap_handle_t m1_demo_get_handle_for(cap_subject_id_t subject);

/*
 * Spawn a module by registered name. Equivalent to:
 *   1. process_create(module.subject, NULL, &pid)
 *   2. cap_table_grant + cap_handle_grant for the module's declared cap
 *      (no-op if the module declares no cap, i.e. m1-unauth)
 *   3. proc_sched_register(pid, module.entry)
 *
 * On success writes the new PID to `*out_pid`. On failure leaves
 * `*out_pid` untouched. `name` MUST be one of "m1-sender",
 * "m1-receiver", or "m1-unauth"; any other value returns
 * MODULE_SPAWN_ERR_UNKNOWN_NAME.
 *
 * The function is intentionally idempotent-free: calling it twice with
 * the same name within one test phase creates two distinct PCBs
 * sharing the same subject id (which is fine — subjects in v0 are not
 * required to be unique across PCBs).
 */
module_spawn_result_t proc_spawn_module(const char *name,
                                        process_id_t *out_pid);

/* ---------------- test-only inspectors (issue #251) --------------- */

/*
 * The demo entry stubs record their observable side effects into the
 * counters below so the test can assert exact behaviour without
 * peeking at scheduler / IPC internals. All counters are zeroed by
 * m1_demo_reset().
 */
typedef struct {
  uint32_t recv_ok;                /* receiver completed ipc_recv_h with OK */
  uint32_t recv_payload_ok;        /* receiver saw expected payload + sender */
  uint32_t recv_sender_subject;    /* sender_subject stamped by kernel */
  uint32_t send_allow_ok;          /* sender's ipc_send_h returned IPC_OK */
  uint32_t send_deny_cap_denied;   /* unauth ipc_send_h returned CAP_DENIED */
} m1_demo_observations_t;

const m1_demo_observations_t *m1_demo_observations_for_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_PROC_MODULE_REGISTRY_H */
