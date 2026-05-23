/**
 * @file process.c
 * @brief Process table implementation for the M1 process abstraction
 *        v0 scaffold (issue #224).
 *
 * Purpose:
 *   Maintains a small fixed-size array of PCB slots indexed by a
 *   16-bit table index, with a 16-bit generation counter packed into
 *   the high half of `process_id_t` to detect use-after-destroy. The
 *   handle layout deliberately mirrors `ipc_port_t` (#220) and
 *   `cap_handle_t` (#237) so the same lifecycle invariants and tests
 *   carry over.
 *
 *   No synchronization primitives: the v0 scaffold is single-threaded
 *   (cooperative scheduling is a separate follow-up). No dynamic
 *   allocation: the PCB array is a static BSS-resident table.
 *
 *   Scope guard — this file MUST NOT grow scheduler state, exit
 *   bookkeeping, IPC integration, or paging fields. Each is its own
 *   sibling issue. Adding any of them here defeats the point of the
 *   single-session execute slice.
 *
 * Interactions:
 *   - process.h: public interface implemented here.
 *   - user/include/secureos_abi.h: anchors OS_ABI_VERSION = 0 for the
 *     static_assert that locks the handle layout.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and into
 *   the host-side process_table test binary.
 *
 * Issue: #224.
 */

#include "process.h"

#include <assert.h>
#include <string.h>

#include "../cap/cap_handle.h"
#include "../../user/include/secureos_abi.h"

/* Handle layout: low 16 bits = table index, high 16 bits = generation.
 * Static-asserted against OS_ABI_VERSION = 0 so any future packing
 * change is a deliberate ABI bump rather than an accidental edit. */
#define PROC_INDEX_MASK 0xFFFFu
#define PROC_GEN_SHIFT 16u

_Static_assert(OS_ABI_VERSION == 0,
               "process_id_t layout is frozen under OS_ABI_VERSION = 0; "
               "any change here requires an ABI bump (#150).");
_Static_assert(sizeof(process_id_t) == 4,
               "process_id_t MUST be exactly 32 bits in OS_ABI_VERSION = 0.");
_Static_assert(PROC_TABLE_MAX <= 0xFFFFu,
               "PROC_TABLE_MAX must fit in the 16-bit index half of "
               "process_id_t.");

typedef struct {
  bool live;
  uint16_t generation;
  cap_subject_id_t subject;
  address_space_t *aspace;
  /* Slice-3 (#250) cooperative-scheduler bookkeeping. These never
   * touch the wire ABI — they are kernel-internal only. */
  process_state_t state;
  proc_entry_fn_t entry;
  uint32_t exit_code;
  const void *blocked_on_port;
} process_slot_t;

static process_slot_t g_procs[PROC_TABLE_MAX];
static bool g_table_initialized = false;

static process_id_t encode_pid(uint16_t index, uint16_t generation) {
  /* Index 0 with generation 0 collides with PID_INVALID, so we always
   * start generations at 1 and bump on each (re)use. */
  return ((process_id_t)generation << PROC_GEN_SHIFT) | (process_id_t)index;
}

static bool decode_pid(process_id_t pid, uint16_t *out_index, uint16_t *out_gen) {
  if (pid == PID_INVALID) {
    return false;
  }
  *out_index = (uint16_t)(pid & PROC_INDEX_MASK);
  *out_gen = (uint16_t)((pid >> PROC_GEN_SHIFT) & 0xFFFFu);
  return *out_index < PROC_TABLE_MAX;
}

static process_slot_t *resolve(process_id_t pid) {
  uint16_t index = 0u;
  uint16_t gen = 0u;
  if (!decode_pid(pid, &index, &gen)) {
    return NULL;
  }
  process_slot_t *slot = &g_procs[index];
  if (!slot->live || slot->generation != gen) {
    return NULL;
  }
  return slot;
}

void process_table_init(void) {
  process_table_reset();
}

void process_table_reset(void) {
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    /* Reset bumps generation so any handle issued before the reset
     * fails on resolve(). Preserve the "never zero" invariant. */
    if (g_table_initialized) {
      g_procs[i].generation = (uint16_t)(g_procs[i].generation + 1u);
      if (g_procs[i].generation == 0u) {
        g_procs[i].generation = 1u;
      }
    } else if (g_procs[i].generation == 0u) {
      g_procs[i].generation = 1u;
    }
    g_procs[i].live = false;
    g_procs[i].subject = 0u;
    g_procs[i].aspace = NULL;
    g_procs[i].state = PROC_STATE_NEW;
    g_procs[i].entry = NULL;
    g_procs[i].exit_code = 0u;
    g_procs[i].blocked_on_port = NULL;
  }
  g_table_initialized = true;
}

proc_result_t process_create(cap_subject_id_t subject,
                             address_space_t *aspace,
                             process_id_t *out_pid) {
  if (!g_table_initialized) {
    process_table_init();
  }
  if (out_pid == NULL) {
    return PROC_ERR_INVALID_ARG;
  }
  *out_pid = PID_INVALID;

  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (!g_procs[i].live) {
      g_procs[i].live = true;
      g_procs[i].subject = subject;
      g_procs[i].aspace = aspace;
      g_procs[i].state = PROC_STATE_NEW;
      g_procs[i].entry = NULL;
      g_procs[i].exit_code = 0u;
      g_procs[i].blocked_on_port = NULL;
      *out_pid = encode_pid(i, g_procs[i].generation);
      return PROC_OK;
    }
  }
  return PROC_ERR_TABLE_FULL;
}

proc_result_t process_destroy(process_id_t pid) {
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  /* M1-CAPTBL-003 (#239): on process exit, bulk-revoke every capability
   * handle owned by this PCB's subject so any stored handle issued before
   * destroy now fails cap_gate_check_handle with CAP_ERR_MISSING. Must
   * happen BEFORE we clear slot->subject. Best-effort; no return-value
   * change from this function. */
  cap_subject_id_t exiting_subject = slot->subject;
  slot->live = false;
  slot->generation = (uint16_t)(slot->generation + 1u);
  if (slot->generation == 0u) {
    slot->generation = 1u;
  }
  slot->subject = 0u;
  slot->aspace = NULL;
  (void)cap_handle_revoke_subject(exiting_subject);
  return PROC_OK;
}

proc_result_t process_lookup(process_id_t pid, process_t *out_proc) {
  if (out_proc == NULL) {
    return PROC_ERR_INVALID_ARG;
  }
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  out_proc->pid = pid;
  out_proc->subject = slot->subject;
  out_proc->aspace = slot->aspace;
  out_proc->state = slot->state;
  out_proc->entry = slot->entry;
  out_proc->exit_code = slot->exit_code;
  out_proc->blocked_on_port = slot->blocked_on_port;
  return PROC_OK;
}

bool process_is_live_for_tests(process_id_t pid) {
  return resolve(pid) != NULL;
}

/* ---------------- slice-3 (#250) mutator accessors ---------------- */

proc_result_t process_set_state(process_id_t pid, process_state_t state) {
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  slot->state = state;
  return PROC_OK;
}

proc_result_t process_set_entry(process_id_t pid, proc_entry_fn_t entry) {
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  slot->entry = entry;
  return PROC_OK;
}

proc_result_t process_set_exit_code(process_id_t pid, uint32_t code) {
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  slot->exit_code = code;
  return PROC_OK;
}

proc_result_t process_set_blocked_on(process_id_t pid, const void *port) {
  process_slot_t *slot = resolve(pid);
  if (slot == NULL) {
    return PROC_ERR_INVALID_PID;
  }
  slot->blocked_on_port = port;
  return PROC_OK;
}
