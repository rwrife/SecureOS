#ifndef SECUREOS_LAUNCHER_H
#define SECUREOS_LAUNCHER_H

#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"
#include "../cap/cap_handle.h"
#include "../proc/address_space.h"
#include "../proc/process.h"

/**
 * @file launcher.h
 * @brief Minimal launcher service that mediates app capability grants.
 *
 * Purpose:
 *   Provides the single launcher-mediated entry point for granting and
 *   exercising the console-write capability on behalf of an app subject.
 *   This is the first vertical slice of the zero-trust launcher described
 *   in plans/2026-04-11-console-launcher-capability-slice.md.
 *
 * Invariants:
 *   - Apps registered with the launcher are deny-by-default for
 *     console output.
 *   - Console output is only authorized when the launcher has explicitly
 *     granted CAP_CONSOLE_WRITE to that app via launcher_grant_console_write().
 *   - Read-only inspection helpers (launcher_app_has_console_write) do not
 *     widen access and do not emit audit grant events.
 */

typedef enum {
  LAUNCHER_OK = 0,
  LAUNCHER_ERR_INVALID_APP = 1,
  LAUNCHER_ERR_NOT_REGISTERED = 2,
  LAUNCHER_ERR_DENIED = 3,
  LAUNCHER_ERR_INTERNAL = 4,
  /* Slice-2 (#269) spawn-path errors. Distinct codes so the
   * launcher_spawn_handoff test can pin which substep of
   * launcher_spawn_app_from_manifest() failed. */
  LAUNCHER_ERR_INVALID_MANIFEST = 5,
  LAUNCHER_ERR_ASPACE = 6,
  LAUNCHER_ERR_PROC_CREATE = 7,
  LAUNCHER_ERR_HANDLE_MINT = 8,
  LAUNCHER_ERR_SCRATCH = 9,
} launcher_result_t;

/*
 * Kernel-internal manifest projection (slice 2 of plan #263, issue #269).
 *
 * The on-disk manifest schema (`manifests/schema/v0.json`) carries fields
 * — id, version, signer key, optional caps, signature — that the
 * build-pipeline validator already gates (#226). The launcher's spawn
 * path only consumes the small subset listed below; we keep this struct
 * in the kernel surface so the in-kernel HelloApp slice (#270) and the
 * host tests both build a manifest in the same shape without dragging a
 * JSON parser into the kernel.
 *
 * Field ownership:
 *   - subject_id           : the spawned app's `cap_subject_id_t`. MUST
 *                            be inside `[1, CAP_TABLE_MAX_SUBJECTS)`.
 *   - auto_grant_caps      : caller-owned array of capability ids the
 *                            launcher is asked to mint a handle for at
 *                            spawn time. v0 supports at most one entry
 *                            (`CAP_CONSOLE_WRITE`) — additional entries
 *                            past index 0 are ignored. The pointer may
 *                            be NULL when `auto_grant_count == 0`.
 *   - auto_grant_count     : number of entries in `auto_grant_caps`.
 *
 * No string fields and no allocation: the struct is plain-old-data and
 * lives on the caller's stack.
 */
typedef struct {
  cap_subject_id_t        subject_id;
  const capability_id_t  *auto_grant_caps;
  size_t                  auto_grant_count;
} launcher_manifest_t;

/*
 * Output of a successful `launcher_spawn_app_from_manifest()` call.
 *
 *   - pid                  : freshly allocated PCB id.
 *   - aspace               : pointer to the partitioned `address_space_t`
 *                            window the launcher carved for the app.
 *                            Owned by the launcher; remains valid until
 *                            `launcher_spawn_reset()` is called.
 *   - granted_handle       : 32-bit `cap_handle_t` minted for the app.
 *                            Equals `CAP_HANDLE_NULL` when the manifest
 *                            requested no auto-grant.
 *   - granted_cap          : the capability id the handle was minted
 *                            against (matches `granted_handle`).
 *
 * The same handle is written little-endian into the first four bytes
 * of `aspace->ipc_scratch` as the M1→M2 initial-handoff vector (see
 * `docs/architecture/m1-m2-handoff.md`).
 */
typedef struct {
  process_id_t      pid;
  address_space_t  *aspace;
  cap_handle_t      granted_handle;
  capability_id_t   granted_cap;
} launcher_spawn_t;

/* Reset launcher state and any registered app grants. Safe for tests. */
void launcher_reset(void);

/*
 * Register an app subject with the launcher so it can later be granted
 * console-write. Registration alone never confers any capability.
 */
launcher_result_t launcher_register_app(cap_subject_id_t app_subject_id);

/*
 * Explicit, launcher-mediated grant of CAP_CONSOLE_WRITE on the app subject.
 * This is the only sanctioned path to widen console output access in this
 * slice. Returns LAUNCHER_ERR_NOT_REGISTERED if the app was never registered.
 */
launcher_result_t launcher_grant_console_write(cap_subject_id_t app_subject_id);

/*
 * Explicit, launcher-mediated revoke. Restores deny-by-default for the app.
 */
launcher_result_t launcher_revoke_console_write(cap_subject_id_t app_subject_id);

/*
 * Sanctioned app output path: routes a console write through the capability
 * gate on behalf of the app subject. Apps that try to bypass this entry
 * point (by writing under their own subject directly) will fail closed
 * because the launcher is the only thing that grants CAP_CONSOLE_WRITE.
 *
 * bytes_written_out may be NULL; on success it is set to the message length.
 */
launcher_result_t launcher_app_console_write(cap_subject_id_t app_subject_id,
                                             const char *message,
                                             size_t *bytes_written_out);

/* Read-only inspection: returns 1 if the app currently holds console-write. */
int launcher_app_has_console_write(cap_subject_id_t app_subject_id);

/* ----------------------------------------------------------------
 * Slice 2 (#269): launcher-mediated spawn + capability handoff.
 *
 * `launcher_spawn_app_from_manifest()` is the single sanctioned entry
 * point that turns a `launcher_manifest_t` into a live PCB:
 *
 *   1. Partitions a fresh `address_space_t` window out of the
 *      launcher-owned arena (`aspace_partition` from #248/#249).
 *   2. Registers the app subject with the launcher (legacy path)
 *      and applies any `auto_grant_at_launch` cap on the legacy
 *      bitset so `cap_check`-based audit parity is preserved.
 *   3. Calls `process_create` with the app subject + new aspace.
 *   4. Mints a `cap_handle_t` for the first `auto_grant_caps[]` entry
 *      (currently only `CAP_CONSOLE_WRITE` is supported) and writes
 *      it little-endian into `aspace->ipc_scratch[0..3]`. This is the
 *      M1→M2 initial-handoff vector documented in
 *      `docs/architecture/m1-m2-handoff.md`.
 *
 * The function does NOT register the spawned PCB with the cooperative
 * scheduler — slice 3 (#270) does that once HelloApp's module body
 * lands. Tests that need a scheduler-aware spawn use `proc_spawn_module`
 * via `kernel/proc/module_registry.c` as before.
 *
 * `out_spawn` MUST be non-NULL. On any error the launcher state is
 * rolled back: the carved aspace slot is returned to the pool, no PCB
 * remains live, and no handle is minted. On success, the caller may
 * call `launcher_spawn_destroy(out_spawn->pid)` to tear the spawn down
 * (which also revokes the minted handle via `process_destroy`'s
 * subject-revoke fast-path from #239).
 *
 * No new ABI surface — error codes and the cap_handle layout are all
 * pre-frozen under `OS_ABI_VERSION = 0`.
 */
launcher_result_t launcher_spawn_app_from_manifest(
    const launcher_manifest_t *manifest,
    launcher_spawn_t *out_spawn);

/* Destroy a spawn produced by `launcher_spawn_app_from_manifest()`. The
 * underlying PCB is destroyed (revoking the minted handle as a side
 * effect, per #239), the carved aspace slot is returned to the spawn
 * pool, and the launcher app-table entry is cleared. Safe to call with
 * `PID_INVALID` (no-op). Returns `LAUNCHER_OK` on either teardown or
 * the no-op case. */
launcher_result_t launcher_spawn_destroy(process_id_t pid);

/* Test helper: reset the launcher spawn pool back to empty. Equivalent
 * to calling `launcher_spawn_destroy()` on every outstanding spawn,
 * but also re-partitions the underlying arena so a subsequent spawn
 * starts from a known-clean state. `launcher_reset()` calls into this. */
void launcher_spawn_reset(void);

/* ----------------------------------------------------------------
 * Slice-2 of plan #277 (M3-SUBSTRATE-002, issue #279): launcher
 * spawn variant that pre-stamps the fs-svc capability handles into
 * the new process's per-process IPC scratch region.
 *
 * Mirrors the M2 console-handle handoff shape (`launcher_spawn_t`),
 * but stamps the fs handles at fixed offsets in `ipc_scratch`:
 *
 *   offset  bytes  meaning
 *      0      4    reserved (M1 single-handle handoff; e.g. console)
 *      4      4    reserved (zero in v0)
 *      8      8    LE64(cap_handle_t)  — CAP_FS_READ handle
 *     16      8    LE64(cap_handle_t)  — CAP_FS_WRITE handle
 *                  (zero / CAP_HANDLE_NULL when `grant_write=false`)
 *
 * The upper 32 bits of each LE64 slot are reserved and MUST be zero
 * in v0 (cap_handle_t is 32-bit under OS_ABI_VERSION=0). Forward-
 * compatibility: a future cap-handle width bump can occupy them
 * without changing slot offsets.
 *
 * The spawned subject is NOT auto-registered with launcher_fs's
 * per-app faux-storage sandbox here: the minted handles authorize
 * the subject against the kernel fs_svc IPC ports via the cap-table
 * gate directly, with no dependency on the launcher_fs ramfs shadow.
 * Wiring the manifest persistence enum (#285 / #286) into a
 * `launcher_fs_register_app(..., persistence)` call belongs in slice
 * 3 (#280); doing it here would force every M2 build that links
 * launcher.c (console, helloapp_qemu) to also drag in launcher_fs.c.
 *
 * No ABI bump: `launcher_result_t` codes, `cap_handle_t` layout, and
 * `address_space_t::ipc_scratch` sizing are all pre-frozen under
 * `OS_ABI_VERSION = 0`.
 */
typedef struct {
  process_id_t      pid;
  address_space_t  *aspace;
  cap_handle_t      read_handle;   /* CAP_FS_READ; non-NULL on success */
  cap_handle_t      write_handle;  /* CAP_FS_WRITE if requested; else CAP_HANDLE_NULL */
} launcher_fs_spawn_t;

launcher_result_t launcher_fs_spawn_app_with_fs_caps(
    const launcher_manifest_t *manifest,
    int grant_write,
    launcher_fs_spawn_t *out_spawn);

/* Destroy a spawn produced by `launcher_fs_spawn_app_with_fs_caps()`.
 * Same contract as `launcher_spawn_destroy()`: PCB tear-down cascades
 * `cap_handle_revoke_subject()` per #239 so the stamped fs handles
 * fail closed after the call. */
launcher_result_t launcher_fs_spawn_destroy(process_id_t pid);

/* ----------------------------------------------------------------
 * Slice-2 of plan #300 (M4-SUBSTRATE-002, issue #303): launcher
 * spawn variant that pre-stamps the broker-svc capability handle
 * into the new process's per-process IPC scratch region.
 *
 * Mirrors the M3 fs-handle handoff shape (`launcher_fs_spawn_t`),
 * but stamps the broker handle at the fixed offset in `ipc_scratch`:
 *
 *   offset  bytes  meaning
 *      0      8    reserved (M1 console single-handle handoff)
 *      8     16    reserved (M3 fs READ/WRITE handle pair)
 *     24      8    LE64(cap_handle_t)  — CAP_IPC_SEND handle for
 *                  broker-svc port (the v0 spelling for the
 *                  broker request-send capability per plan
 *                  §"Capability id for the broker-svc port" opt 1)
 *     32     32    reserved / available for normal IPC send buffer
 *
 * Owner / recipient apps both consume the same handle; broker
 * authority is enforced inside `cap_broker_*` against the
 * `sender_subject` field on the inbound request, not at the gate.
 *
 * The upper 32 bits of the LE64 slot are reserved and MUST be zero
 * in v0 (cap_handle_t is 32-bit under OS_ABI_VERSION=0).
 *
 * No ABI bump: `launcher_result_t` codes, `cap_handle_t` layout,
 * and `address_space_t::ipc_scratch` sizing are all pre-frozen
 * under `OS_ABI_VERSION = 0`. `ipc_scratch` is kernel-internal.
 */
typedef struct {
  process_id_t      pid;
  address_space_t  *aspace;
  cap_handle_t      broker_handle;  /* CAP_IPC_SEND; non-NULL on success */
} launcher_broker_spawn_t;

launcher_result_t launcher_broker_spawn_app_with_broker_cap(
    const launcher_manifest_t *manifest,
    launcher_broker_spawn_t *out_spawn);

/* Destroy a spawn produced by `launcher_broker_spawn_app_with_broker_cap()`.
 * Same contract as `launcher_spawn_destroy()`: PCB tear-down cascades
 * `cap_handle_revoke_subject()` per #239 so the stamped broker handle
 * fails closed after the call. */
launcher_result_t launcher_broker_spawn_destroy(process_id_t pid);

#endif
