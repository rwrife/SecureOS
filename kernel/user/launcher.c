/**
 * @file launcher.c
 * @brief Launcher-mediated capability slice for console-write.
 *
 * Purpose:
 *   Implements the minimal launcher described in
 *   plans/2026-04-11-console-launcher-capability-slice.md. Apps must be
 *   registered with the launcher, and CAP_CONSOLE_WRITE must be granted via
 *   the launcher before any console output is permitted. Read-only inspection
 *   never widens access.
 *
 * Interactions:
 *   - cap_table.c: launcher_grant_console_write/launcher_revoke_console_write
 *     update the capability table on behalf of the app subject.
 *   - cap_gate.c: launcher_app_console_write routes through
 *     cap_console_write_gate so the deny-by-default policy is enforced.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and reset at
 *   boot or by tests via launcher_reset().
 */

#include "launcher.h"

#include <stddef.h>
#include <string.h>

#include "../cap/cap_gate.h"
#include "../cap/cap_handle.h"
#include "../cap/cap_table.h"
#include "../ipc/ipc_msg.h"
#include "../proc/address_space.h"
#include "../proc/process.h"

#define LAUNCHER_MAX_APPS CAP_TABLE_MAX_SUBJECTS

typedef struct {
  int used;
  cap_subject_id_t subject_id;
} launcher_app_entry_t;

static launcher_app_entry_t launcher_apps[LAUNCHER_MAX_APPS];

static launcher_app_entry_t *launcher_find_entry(cap_subject_id_t app_subject_id) {
  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (launcher_apps[i].used && launcher_apps[i].subject_id == app_subject_id) {
      return &launcher_apps[i];
    }
  }
  return 0;
}

static int launcher_subject_in_range(cap_subject_id_t app_subject_id) {
  return app_subject_id < CAP_TABLE_MAX_SUBJECTS;
}

void launcher_reset(void) {
  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (launcher_apps[i].used) {
      /* Best-effort revoke so a reset cannot leave stale capabilities. */
      (void)cap_table_revoke(launcher_apps[i].subject_id, CAP_CONSOLE_WRITE);
    }
    launcher_apps[i].used = 0;
    launcher_apps[i].subject_id = 0u;
  }
  launcher_spawn_reset();
}

launcher_result_t launcher_register_app(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) != 0) {
    return LAUNCHER_OK;
  }

  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (!launcher_apps[i].used) {
      launcher_apps[i].used = 1;
      launcher_apps[i].subject_id = app_subject_id;
      return LAUNCHER_OK;
    }
  }

  return LAUNCHER_ERR_INTERNAL;
}

launcher_result_t launcher_grant_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  if (cap_table_grant(app_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return LAUNCHER_ERR_INTERNAL;
  }

  return LAUNCHER_OK;
}

launcher_result_t launcher_revoke_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  if (cap_table_revoke(app_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return LAUNCHER_ERR_INTERNAL;
  }

  return LAUNCHER_OK;
}

launcher_result_t launcher_app_console_write(cap_subject_id_t app_subject_id,
                                             const char *message,
                                             size_t *bytes_written_out) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  cap_result_t gate_result = cap_console_write_gate(app_subject_id, message, bytes_written_out);
  if (gate_result != CAP_OK) {
    return LAUNCHER_ERR_DENIED;
  }

  return LAUNCHER_OK;
}

int launcher_app_has_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return 0;
  }
  if (launcher_find_entry(app_subject_id) == 0) {
    return 0;
  }
  return cap_table_check(app_subject_id, CAP_CONSOLE_WRITE) == CAP_OK;
}

/* ----------------------------------------------------------------
 * Slice 2 (#269): launcher-mediated spawn + capability handoff.
 *
 * The launcher owns a small, statically-sized arena out of which it
 * carves one `address_space_t` window per spawned app via
 * `aspace_partition`. The arena is sized for `LAUNCHER_SPAWN_MAX`
 * concurrent spawns, matching the launcher's app-registration cap.
 *
 * Rationale for an arena local to the launcher (rather than reusing
 * `module_registry.c`'s `g_demo_aspace_arena`): the M1 IPC demo lives
 * in `module_registry.c` and runs in the same test phase as the M2
 * substrate slice 2 acceptance test (#269). Sharing a single arena
 * would let the two callers race on `g_demo_aspace_next`. A separate,
 * launcher-owned arena keeps the slices independent so neither side
 * has to know about the other.
 * ---------------------------------------------------------------- */

#define LAUNCHER_SPAWN_MAX  CAP_TABLE_MAX_SUBJECTS

/* Per-window: kernel stack + IPC scratch + alignment slack. Matches the
 * sizing used by `module_registry.c` (`PROC_KSTACK_BYTES +
 * IPC_MSG_PAYLOAD_MAX + 64`). */
#define LAUNCHER_SPAWN_ARENA_BYTES                                       \
  (LAUNCHER_SPAWN_MAX *                                                  \
   (PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64u))

typedef struct {
  bool             in_use;
  process_id_t     pid;
  address_space_t *aspace;
  cap_handle_t     handle;
  capability_id_t  cap;
} launcher_spawn_slot_t;

static uint8_t                g_launcher_arena[LAUNCHER_SPAWN_ARENA_BYTES];
static address_space_t        g_launcher_aspaces[LAUNCHER_SPAWN_MAX];
static launcher_spawn_slot_t  g_launcher_spawns[LAUNCHER_SPAWN_MAX];
static size_t                 g_launcher_aspace_next = 0u;
static bool                   g_launcher_arena_ready = false;

static bool launcher_arena_rebuild(void) {
  aspace_result_t pr = aspace_partition(
      (uintptr_t)g_launcher_arena, sizeof(g_launcher_arena),
      g_launcher_aspaces, LAUNCHER_SPAWN_MAX);
  g_launcher_aspace_next = 0u;
  g_launcher_arena_ready = (pr == ASPACE_OK);
  return g_launcher_arena_ready;
}

static address_space_t *launcher_aspace_claim(void) {
  if (!g_launcher_arena_ready) {
    (void)launcher_arena_rebuild();
  }
  if (!g_launcher_arena_ready) {
    return NULL;
  }
  if (g_launcher_aspace_next >= LAUNCHER_SPAWN_MAX) {
    return NULL;
  }
  return &g_launcher_aspaces[g_launcher_aspace_next++];
}

static launcher_spawn_slot_t *launcher_spawn_slot_alloc(void) {
  for (size_t i = 0; i < LAUNCHER_SPAWN_MAX; ++i) {
    if (!g_launcher_spawns[i].in_use) {
      return &g_launcher_spawns[i];
    }
  }
  return NULL;
}

static launcher_spawn_slot_t *launcher_spawn_slot_find(process_id_t pid) {
  if (pid == PID_INVALID) {
    return NULL;
  }
  for (size_t i = 0; i < LAUNCHER_SPAWN_MAX; ++i) {
    if (g_launcher_spawns[i].in_use && g_launcher_spawns[i].pid == pid) {
      return &g_launcher_spawns[i];
    }
  }
  return NULL;
}

void launcher_spawn_reset(void) {
  for (size_t i = 0; i < LAUNCHER_SPAWN_MAX; ++i) {
    if (g_launcher_spawns[i].in_use && g_launcher_spawns[i].pid != PID_INVALID) {
      /* Best-effort tear-down; ignore errors so reset is total. */
      (void)process_destroy(g_launcher_spawns[i].pid);
    }
    g_launcher_spawns[i].in_use = false;
    g_launcher_spawns[i].pid = PID_INVALID;
    g_launcher_spawns[i].aspace = NULL;
    g_launcher_spawns[i].handle = CAP_HANDLE_NULL;
    g_launcher_spawns[i].cap = (capability_id_t)0;
  }
  (void)launcher_arena_rebuild();
}

/* Encode a 32-bit handle as little-endian into the first four bytes of
 * the per-process IPC scratch region. This is the M1→M2 initial-handoff
 * vector documented in `docs/architecture/m1-m2-handoff.md`. */
static void scratch_store_handle(address_space_t *as, cap_handle_t h) {
  if (as == NULL || as->ipc_scratch == NULL) {
    return;
  }
  uint8_t *p = as->ipc_scratch;
  p[0] = (uint8_t)( h        & 0xFFu);
  p[1] = (uint8_t)((h >>  8) & 0xFFu);
  p[2] = (uint8_t)((h >> 16) & 0xFFu);
  p[3] = (uint8_t)((h >> 24) & 0xFFu);
}

launcher_result_t launcher_spawn_app_from_manifest(
    const launcher_manifest_t *manifest,
    launcher_spawn_t *out_spawn) {
  if (out_spawn == NULL) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  out_spawn->pid = PID_INVALID;
  out_spawn->aspace = NULL;
  out_spawn->granted_handle = CAP_HANDLE_NULL;
  out_spawn->granted_cap = (capability_id_t)0;

  if (manifest == NULL) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  if (!launcher_subject_in_range(manifest->subject_id) ||
      manifest->subject_id == 0u) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  if (manifest->auto_grant_count > 0 && manifest->auto_grant_caps == NULL) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }

  /* v0 supports at most one auto-granted cap; ignore extras silently
   * the same way the on-disk schema reserves room for them. */
  bool want_grant = (manifest->auto_grant_count > 0);
  capability_id_t grant_cap = want_grant ? manifest->auto_grant_caps[0]
                                         : (capability_id_t)0;
  if (want_grant) {
    /* Mirror `launcher_grant_console_write`'s ABI: slice 2 only knows
     * how to hand off CAP_CONSOLE_WRITE in the scratch slot. Other
     * caps would need their own slice-3 wiring. */
    if (grant_cap != CAP_CONSOLE_WRITE) {
      return LAUNCHER_ERR_INVALID_MANIFEST;
    }
  }

  launcher_spawn_slot_t *slot = launcher_spawn_slot_alloc();
  if (slot == NULL) {
    return LAUNCHER_ERR_INTERNAL;
  }

  /* (1) Carve a fresh aspace window. */
  address_space_t *as = launcher_aspace_claim();
  if (as == NULL) {
    return LAUNCHER_ERR_ASPACE;
  }

  /* (2) Register the subject with the launcher's legacy app table so
   * the existing `launcher_app_console_write` audit path stays intact. */
  launcher_result_t reg = launcher_register_app(manifest->subject_id);
  if (reg != LAUNCHER_OK) {
    return reg;
  }

  /* (3) Spawn the PCB. */
  process_id_t pid = PID_INVALID;
  if (process_create(manifest->subject_id, as, &pid) != PROC_OK) {
    return LAUNCHER_ERR_PROC_CREATE;
  }

  /* (4) Zero the scratch slot so the handoff contract's reserved-bytes
   * guarantee holds regardless of arena re-use. If a cap is being
   * handed off, the LE handle write below clobbers bytes [0..4). */
  if (as->ipc_scratch != NULL) {
    memset(as->ipc_scratch, 0, IPC_MSG_PAYLOAD_MAX);
  }

  /* (5) If a cap was requested, mint both the legacy bitset grant
   * (so cap_check audit parity is preserved on ipc_send_h paths) and
   * the authoritative handle, then store the handle LE into the
   * scratch slot. */
  cap_handle_t h = CAP_HANDLE_NULL;
  if (want_grant) {
    if (cap_table_grant(manifest->subject_id, grant_cap) != CAP_OK) {
      (void)process_destroy(pid);
      return LAUNCHER_ERR_HANDLE_MINT;
    }
    h = cap_handle_grant(manifest->subject_id, grant_cap);
    if (h == CAP_HANDLE_NULL) {
      (void)process_destroy(pid);
      return LAUNCHER_ERR_HANDLE_MINT;
    }
    if (as->ipc_scratch == NULL) {
      /* Aspace carry no scratch — cannot complete the handoff. */
      (void)process_destroy(pid);
      return LAUNCHER_ERR_SCRATCH;
    }
    scratch_store_handle(as, h);
  }

  slot->in_use = true;
  slot->pid = pid;
  slot->aspace = as;
  slot->handle = h;
  slot->cap = grant_cap;

  out_spawn->pid = pid;
  out_spawn->aspace = as;
  out_spawn->granted_handle = h;
  out_spawn->granted_cap = grant_cap;
  return LAUNCHER_OK;
}

launcher_result_t launcher_spawn_destroy(process_id_t pid) {
  if (pid == PID_INVALID) {
    return LAUNCHER_OK;
  }
  launcher_spawn_slot_t *slot = launcher_spawn_slot_find(pid);
  if (slot == NULL) {
    /* Not a launcher-owned spawn; refuse rather than silently destroy
     * an arbitrary PCB. */
    return LAUNCHER_ERR_INVALID_APP;
  }
  /* process_destroy cascades cap_handle_revoke_subject() per #239 so
   * any stored copy of the minted handle fails cleanly after this. */
  (void)process_destroy(slot->pid);
  slot->in_use = false;
  slot->pid = PID_INVALID;
  slot->aspace = NULL;
  slot->handle = CAP_HANDLE_NULL;
  slot->cap = (capability_id_t)0;
  return LAUNCHER_OK;
}

/* ----------------------------------------------------------------
 * M3-SUBSTRATE-002 (issue #279): launcher-mediated spawn variant
 * that pre-stamps fs-svc capability handles into the spawned
 * process's per-process ipc_scratch region.
 *
 * The single launcher spawn pool above is shared: an fs-cap spawn
 * occupies one slot exactly like a console spawn. The slot's
 * `handle`/`cap` fields are reused to track the fs READ handle
 * (which is always minted); the WRITE handle is tracked implicitly
 * by `cap_handle_revoke_subject()` at destroy time — we don't need
 * a second slot field because process_destroy authoritatively kills
 * every handle owned by the subject (#239).
 * ---------------------------------------------------------------- */

/* Write a 32-bit cap_handle_t as a little-endian 64-bit value into
 * `bytes` (which MUST point to 8 writable bytes). The upper 32 bits
 * are zeroed — reserved under OS_ABI_VERSION=0 per the handoff doc. */
static void scratch_store_handle_le64(uint8_t *bytes, cap_handle_t h) {
  if (bytes == NULL) {
    return;
  }
  bytes[0] = (uint8_t)( h        & 0xFFu);
  bytes[1] = (uint8_t)((h >>  8) & 0xFFu);
  bytes[2] = (uint8_t)((h >> 16) & 0xFFu);
  bytes[3] = (uint8_t)((h >> 24) & 0xFFu);
  bytes[4] = 0u;
  bytes[5] = 0u;
  bytes[6] = 0u;
  bytes[7] = 0u;
}

launcher_result_t launcher_fs_spawn_app_with_fs_caps(
    const launcher_manifest_t *manifest,
    int grant_write,
    launcher_fs_spawn_t *out_spawn) {
  if (out_spawn == NULL) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  out_spawn->pid = PID_INVALID;
  out_spawn->aspace = NULL;
  out_spawn->read_handle = CAP_HANDLE_NULL;
  out_spawn->write_handle = CAP_HANDLE_NULL;

  if (manifest == NULL) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  if (!launcher_subject_in_range(manifest->subject_id) ||
      manifest->subject_id == 0u) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }
  /* The fs-spawn path does not consume `auto_grant_caps` — fs handles
   * are minted from the boolean `grant_write` argument plus the
   * always-on CAP_FS_READ. Reject a non-zero auto_grant_count so the
   * caller can't smuggle in a console grant by accident. */
  if (manifest->auto_grant_count != 0u) {
    return LAUNCHER_ERR_INVALID_MANIFEST;
  }

  launcher_spawn_slot_t *slot = launcher_spawn_slot_alloc();
  if (slot == NULL) {
    return LAUNCHER_ERR_INTERNAL;
  }

  /* (1) Carve a fresh aspace window. */
  address_space_t *as = launcher_aspace_claim();
  if (as == NULL) {
    return LAUNCHER_ERR_ASPACE;
  }
  if (as->ipc_scratch == NULL) {
    return LAUNCHER_ERR_SCRATCH;
  }

  /* (2) Spawn the PCB. */
  process_id_t pid = PID_INVALID;
  if (process_create(manifest->subject_id, as, &pid) != PROC_OK) {
    return LAUNCHER_ERR_PROC_CREATE;
  }

  /* (3) The spawned subject is intentionally NOT auto-registered
   * with launcher_fs's per-app faux-storage sandbox here. The fs
   * handles minted below authorize the subject against the kernel
   * fs_svc IPC ports directly via the cap-table gate — they do not
   * need the launcher_fs ramfs-shadow to exist. Slice 3 (#280) is
   * the right place to wire per-app persistence policy once the
   * manifest persistence enum (#285 / #286) is consumed here. This
   * also keeps launcher.c free of a hard launcher_fs.c link
   * dependency, so the existing M2 builds (console, helloapp_qemu)
   * stay linkable without dragging in the faux-fs surface.
   */

  /* (4) Zero the full scratch slot so reserved-bytes guarantees hold
   * regardless of arena re-use. The LE writes below clobber bytes
   * [8..24); bytes [0..8) and [24..64) are left zero. */
  memset(as->ipc_scratch, 0, IPC_MSG_PAYLOAD_MAX);

  /* (5) Mint the CAP_FS_READ handle (always) and, if requested, the
   * CAP_FS_WRITE handle. Keep the legacy bitset grant in sync so
   * audit-trail tests that go through cap_check still see the grant. */
  if (cap_table_grant(manifest->subject_id, CAP_FS_READ) != CAP_OK) {
    (void)process_destroy(pid);
    return LAUNCHER_ERR_HANDLE_MINT;
  }
  cap_handle_t read_h = cap_handle_grant(manifest->subject_id, CAP_FS_READ);
  if (read_h == CAP_HANDLE_NULL) {
    (void)process_destroy(pid);
    return LAUNCHER_ERR_HANDLE_MINT;
  }

  cap_handle_t write_h = CAP_HANDLE_NULL;
  if (grant_write) {
    if (cap_table_grant(manifest->subject_id, CAP_FS_WRITE) != CAP_OK) {
      (void)process_destroy(pid);
      return LAUNCHER_ERR_HANDLE_MINT;
    }
    write_h = cap_handle_grant(manifest->subject_id, CAP_FS_WRITE);
    if (write_h == CAP_HANDLE_NULL) {
      (void)process_destroy(pid);
      return LAUNCHER_ERR_HANDLE_MINT;
    }
  }

  /* (6) Stamp the handles into ipc_scratch at the slice-2 offsets.
   * IPC_MSG_PAYLOAD_MAX == 64 so [8..16) and [16..24) are always
   * in-bounds; assert via the runtime check on the aspace above. */
  uint8_t *p = as->ipc_scratch;
  scratch_store_handle_le64(&p[8], read_h);
  scratch_store_handle_le64(&p[16], write_h);

  slot->in_use = true;
  slot->pid = pid;
  slot->aspace = as;
  slot->handle = read_h;
  slot->cap = CAP_FS_READ;

  out_spawn->pid = pid;
  out_spawn->aspace = as;
  out_spawn->read_handle = read_h;
  out_spawn->write_handle = write_h;
  return LAUNCHER_OK;
}

launcher_result_t launcher_fs_spawn_destroy(process_id_t pid) {
  /* Identical contract to `launcher_spawn_destroy()`: the slot pool
   * is shared so a single teardown path handles both spawn variants.
   * process_destroy() cascades cap_handle_revoke_subject() (#239) so
   * both the read and write handles fail cleanly post-call. */
  return launcher_spawn_destroy(pid);
}
