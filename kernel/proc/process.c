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
#include <stdio.h>
#include <string.h>

#include "../cap/cap_deny_marker.h"
#include "../cap/cap_handle.h"
#include "../../user/include/secureos_abi.h"

/* Canonical CAP:DENY:<subject>:app_exec:<resource> marker emitter for
 * resource-exhaustion denies on process creation (issue #261).
 *
 * Design notes (decision recorded inline because this is the only
 * non-cap-gate deny path that currently rides the cap_deny_marker):
 *
 *   The PROC_TABLE_FULL deny is structurally a resource-exhaustion
 *   condition, not a cap_check() miss. But issue #261 explicitly asks
 *   for cross-cutting greppability through the same canonical
 *   CAP:DENY:<...> marker the IPC and syscall deny paths already use
 *   (#167 / #211 / #221 / PR #244). The marker grammar locked by
 *   docs/abi/capability-deny-contract.md §4 + cap_deny_marker_validate
 *   requires field 0 to be a decimal cap_subject_id_t and field 1 to be
 *   a registered name in cdm_cap_names[]. "proc_table" / "process_create"
 *   as proposed in the issue body would fail both checks.
 *
 *   The minimum-blast-radius resolution that satisfies the spirit of
 *   the issue (single greppable shape, zero ABI churn, passes the #221
 *   conformance test unmodified) is to reuse CAP_APP_EXEC — the cap the
 *   M2 launcher will gate process spawn on — with the would-be
 *   subject in field 0 and the literal "proc_table_full" in the
 *   resource slot. M2 launcher-mediated greps for
 *   `CAP:DENY:*:app_exec:*` therefore observe both real policy denies
 *   and kernel exhaustion denies with the same predicate.
 *
 *   See the long comment thread on #261 for the full A/B/C/D options
 *   considered; this is option A. */
#define PROC_CREATE_DENY_RESOURCE_TABLE_FULL "proc_table_full"

static void proc_emit_table_full_deny_marker(cap_subject_id_t subject) {
  char buf[CAP_DENY_MARKER_MAX];
  int n = cap_deny_marker_format(subject, CAP_APP_EXEC,
                                 PROC_CREATE_DENY_RESOURCE_TABLE_FULL,
                                 buf, sizeof(buf));
  if (n > 0) {
    /* fwrite preserves the trailing '\n' the formatter already wrote.
     * Matches the kernel/ipc/ipc_ops.c emission discipline so the
     * conformance test's stdout-scan harness works unchanged. */
    (void)fwrite(buf, 1u, (size_t)n, stdout);
  }
}

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
  /* All slots live: emit the canonical CAP:DENY:<subject>:app_exec:proc_table_full
   * marker (issue #261) before returning the exhaustion error. The emission
   * is one-per-attempt; idempotency is the caller's responsibility. */
  proc_emit_table_full_deny_marker(subject);
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

address_space_t *process_find_aspace_by_subject(cap_subject_id_t subject) {
  /* Subject 0 is the v0 "unknown / unset" sentinel: callers must never
   * see a non-NULL aspace for it. process_create stores whatever the
   * caller passes (including 0), so we filter here rather than at
   * create-time to avoid changing process_create's documented
   * contract. */
  if (subject == 0u) {
    return NULL;
  }
  if (!g_table_initialized) {
    return NULL;
  }
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    process_slot_t *slot = &g_procs[i];
    if (slot->live && slot->subject == subject) {
      return slot->aspace;
    }
  }
  return NULL;
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
