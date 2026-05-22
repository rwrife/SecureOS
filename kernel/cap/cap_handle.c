/**
 * @file cap_handle.c
 * @brief M1 kernel capability handle table skeleton — implementation.
 *
 * Purpose:
 *   First slice (M1-CAPTBL-001) of the plan in
 *   plans/2026-05-20-m1-kernel-capability-table.md. Lands the
 *   `cap_handle_row` struct, the bounded global table, and a minimal
 *   grant/revoke/check API. Existing `cap_table_*` call sites are NOT
 *   migrated in this slice — that is deferred per plan acceptance #2 to a
 *   later execute issue that wires the façade and proves byte-exact audit
 *   parity.
 *
 * Interactions:
 *   - capability.h: shares `capability_id_t`, `cap_subject_id_t`, and the
 *     `cap_result_t` vocabulary used by every kernel cap call site.
 *   - cap_table.{c,h}: untouched in this slice. The handle table sits
 *     alongside the existing per-subject bitset table.
 *
 * Launched by:
 *   Not yet wired into kmain() — this slice is library-only and exercised
 *   by tests/cap_table_skeleton_test.c via
 *   build/scripts/test_cap_table_skeleton.sh.
 *
 * Determinism / no-heap:
 *   - Static `.bss` allocation (`g_rows[CAP_HANDLE_TABLE_MAX]`).
 *   - No `kmalloc`, no syscalls, no global mutable state outside the table.
 */

#include "cap_handle.h"

#include <stddef.h>
#include <stdint.h>

/* The plan freezes the handle representation against OS_ABI_VERSION=0
 * (#150). M1-CAPTBL-002 (#233) flips the local constant to the canonical
 * include-and-pin so any future ABI bump is caught by the static_assert in
 * cap_handle.h. */
#define CAP_HANDLE_ABI_VERSION_V0 ((uint16_t)OS_ABI_VERSION)

static cap_handle_row g_rows[CAP_HANDLE_TABLE_MAX];

static int cap_handle_subject_valid(cap_subject_id_t subject_id) {
  return subject_id < CAP_TABLE_MAX_SUBJECTS;
}

static int cap_handle_cap_id_valid(capability_id_t cap_id) {
  return cap_id >= CAP_CONSOLE_WRITE && cap_id <= CAP_IPC_RECV;
}

static cap_handle_row *cap_handle_find_live(cap_subject_id_t owner_subject,
                                            capability_id_t cap_id) {
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    cap_handle_row *row = &g_rows[i];
    if ((row->flags & CAP_HANDLE_FLAG_LIVE) == 0u) {
      continue;
    }
    if (row->owner_subject == owner_subject && row->cap_id == cap_id) {
      return row;
    }
  }
  return NULL;
}

static cap_handle_row *cap_handle_find_free_slot(void) {
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    if ((g_rows[i].flags & CAP_HANDLE_FLAG_LIVE) == 0u &&
        (g_rows[i].flags & CAP_HANDLE_FLAG_REVOKED) == 0u) {
      return &g_rows[i];
    }
  }
  /* Second pass: a revoked row may be reused, but the row retains its
   * generation counter so prior handles continue to fail the M1-CAPTBL-002
   * staleness check once it lands. */
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    if ((g_rows[i].flags & CAP_HANDLE_FLAG_LIVE) == 0u) {
      return &g_rows[i];
    }
  }
  return NULL;
}

void cap_handle_table_init(void) {
  cap_handle_table_reset();
}

void cap_handle_table_reset(void) {
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    g_rows[i].abi_version = 0u;
    g_rows[i].flags = 0u;
    g_rows[i].cap_id = (capability_id_t)0;
    g_rows[i].owner_subject = 0u;
    g_rows[i].granter_subject = 0u;
    g_rows[i].parent_handle = 0u;
    g_rows[i].generation = 0u;
  }
}

cap_result_t cap_handle_table_grant(cap_subject_id_t owner_subject,
                                    cap_subject_id_t granter_subject,
                                    capability_id_t cap_id) {
  if (!cap_handle_subject_valid(owner_subject) ||
      !cap_handle_subject_valid(granter_subject)) {
    return CAP_ERR_SUBJECT_INVALID;
  }
  if (!cap_handle_cap_id_valid(cap_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  /* Idempotent duplicate-grant path (plan §"Mapping the M2 console grants
   * into the table"). No second row, no generation bump. */
  if (cap_handle_find_live(owner_subject, cap_id) != NULL) {
    return CAP_OK;
  }

  cap_handle_row *row = cap_handle_find_free_slot();
  if (row == NULL) {
    /* Table full — plan §"Risks": v0 treats this as a hard CAP error.
     * CAP_ERR_CAP_INVALID is the closest existing code; a dedicated
     * CAP_ERR_TABLE_FULL is a candidate for the M1-CAPTBL-002 follow-up. */
    return CAP_ERR_CAP_INVALID;
  }

  row->abi_version = CAP_HANDLE_ABI_VERSION_V0;
  row->cap_id = cap_id;
  row->owner_subject = owner_subject;
  row->granter_subject = granter_subject;
  row->parent_handle = 0u; /* reserved in v0 */
  row->generation += 1u;
  row->flags = CAP_HANDLE_FLAG_LIVE;
  return CAP_OK;
}

cap_result_t cap_handle_table_revoke(cap_subject_id_t owner_subject,
                                     capability_id_t cap_id) {
  if (!cap_handle_subject_valid(owner_subject)) {
    return CAP_ERR_SUBJECT_INVALID;
  }
  if (!cap_handle_cap_id_valid(cap_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  cap_handle_row *row = cap_handle_find_live(owner_subject, cap_id);
  if (row == NULL) {
    /* Revoke of an ungranted (subject, cap) — clear missing marker, no-op
     * on table state (plan §"Revocation hooks"). */
    return CAP_ERR_MISSING;
  }

  row->flags = CAP_HANDLE_FLAG_REVOKED;
  row->generation += 1u;
  return CAP_OK;
}

cap_result_t cap_handle_table_check(cap_subject_id_t owner_subject,
                                    capability_id_t cap_id) {
  if (!cap_handle_subject_valid(owner_subject)) {
    return CAP_ERR_SUBJECT_INVALID;
  }
  if (!cap_handle_cap_id_valid(cap_id)) {
    return CAP_ERR_CAP_INVALID;
  }
  return (cap_handle_find_live(owner_subject, cap_id) != NULL)
             ? CAP_OK
             : CAP_ERR_MISSING;
}

uint32_t cap_handle_table_live_count(void) {
  uint32_t live = 0u;
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    if ((g_rows[i].flags & CAP_HANDLE_FLAG_LIVE) != 0u) {
      live += 1u;
    }
  }
  return live;
}

/* ----------------------------------------------------------------------
 * M1-CAPTBL-002: 32-bit packed handle API (issue #233).
 *
 * Handles are derived from the row's slot index and low-14-bits of its
 * generation counter. Revocation bumps the generation, so the prior handle
 * value fails the staleness check on the next gate. The row is retained
 * (slot is not freed in v0) so the generation counter remains observable
 * after revoke — plan acceptance #4.
 * --------------------------------------------------------------------*/

static uint16_t cap_handle_row_slot(const cap_handle_row *row) {
  /* g_rows is a fixed array; pointer arithmetic gives the slot index. */
  return (uint16_t)(row - g_rows);
}

cap_handle_t cap_handle_pack(uint16_t slot, uint16_t generation_low14,
                             uint8_t tag) {
  cap_handle_t h = 0u;
  h |= ((cap_handle_t)slot & CAP_HANDLE_SLOT_MASK) << CAP_HANDLE_SLOT_SHIFT;
  h |= ((cap_handle_t)generation_low14 & CAP_HANDLE_GEN_MASK)
       << CAP_HANDLE_GEN_SHIFT;
  h |= ((cap_handle_t)tag & CAP_HANDLE_TAG_MASK) << CAP_HANDLE_TAG_SHIFT;
  return h;
}

uint16_t cap_handle_slot(cap_handle_t handle) {
  return (uint16_t)((handle >> CAP_HANDLE_SLOT_SHIFT) & CAP_HANDLE_SLOT_MASK);
}

uint16_t cap_handle_generation(cap_handle_t handle) {
  return (uint16_t)((handle >> CAP_HANDLE_GEN_SHIFT) & CAP_HANDLE_GEN_MASK);
}

uint8_t cap_handle_tag(cap_handle_t handle) {
  return (uint8_t)((handle >> CAP_HANDLE_TAG_SHIFT) & CAP_HANDLE_TAG_MASK);
}

static cap_handle_t cap_handle_from_row(const cap_handle_row *row) {
  return cap_handle_pack(cap_handle_row_slot(row),
                         (uint16_t)(row->generation & CAP_HANDLE_GEN_MASK),
                         (uint8_t)CAP_HANDLE_TAG_KERNEL);
}

cap_handle_t cap_handle_grant(cap_subject_id_t owner_subject,
                              capability_id_t cap_id) {
  if (!cap_handle_subject_valid(owner_subject) ||
      !cap_handle_cap_id_valid(cap_id)) {
    return CAP_HANDLE_NULL;
  }

  /* Idempotent: if a live row already exists, return its current handle. */
  cap_handle_row *existing = cap_handle_find_live(owner_subject, cap_id);
  if (existing != NULL) {
    return cap_handle_from_row(existing);
  }

  cap_handle_row *row = cap_handle_find_free_slot();
  if (row == NULL) {
    return CAP_HANDLE_NULL;
  }

  row->abi_version = CAP_HANDLE_ABI_VERSION_V0;
  row->cap_id = cap_id;
  row->owner_subject = owner_subject;
  row->granter_subject = owner_subject; /* one-arg form; M4 broker will widen */
  row->parent_handle = 0u;
  row->generation += 1u;
  row->flags = CAP_HANDLE_FLAG_LIVE;
  return cap_handle_from_row(row);
}

cap_result_t cap_gate_check_handle_result(cap_handle_t handle,
                                          capability_id_t expected_cap) {
  if (cap_handle_tag(handle) != CAP_HANDLE_TAG_KERNEL) {
    return CAP_ERR_CAP_INVALID;
  }
  if (!cap_handle_cap_id_valid(expected_cap)) {
    return CAP_ERR_CAP_INVALID;
  }
  uint16_t slot = cap_handle_slot(handle);
  if (slot >= CAP_HANDLE_TABLE_MAX) {
    return CAP_ERR_CAP_INVALID;
  }
  cap_handle_row *row = &g_rows[slot];
  if ((row->flags & CAP_HANDLE_FLAG_LIVE) == 0u) {
    return CAP_ERR_MISSING;
  }
  if ((row->generation & CAP_HANDLE_GEN_MASK) != cap_handle_generation(handle)) {
    return CAP_ERR_MISSING;
  }
  if (row->cap_id != expected_cap) {
    return CAP_ERR_CAP_INVALID;
  }
  return CAP_OK;
}

int cap_gate_check_handle(cap_handle_t handle, capability_id_t expected_cap) {
  return cap_gate_check_handle_result(handle, expected_cap) == CAP_OK;
}

cap_result_t cap_handle_revoke(cap_handle_t handle) {
  if (cap_handle_tag(handle) != CAP_HANDLE_TAG_KERNEL) {
    return CAP_ERR_CAP_INVALID;
  }
  uint16_t slot = cap_handle_slot(handle);
  if (slot >= CAP_HANDLE_TABLE_MAX) {
    return CAP_ERR_CAP_INVALID;
  }
  cap_handle_row *row = &g_rows[slot];
  if ((row->flags & CAP_HANDLE_FLAG_LIVE) == 0u) {
    return CAP_ERR_MISSING;
  }
  if ((row->generation & CAP_HANDLE_GEN_MASK) != cap_handle_generation(handle)) {
    /* Stale handle revoke is a no-op. */
    return CAP_ERR_MISSING;
  }
  row->flags = CAP_HANDLE_FLAG_REVOKED;
  row->generation += 1u;
  return CAP_OK;
}

/* ----------------------------------------------------------------------
 * M1-CAPTBL-003: bulk revoke by owner (issue #239).
 *
 * Single pass over the global table. Each live row whose `owner_subject`
 * matches is transitioned LIVE -> REVOKED with its generation bumped, so
 * any previously-issued handle for that row now fails the staleness check
 * in `cap_gate_check_handle_result`. Best-effort by contract: a bad
 * subject id (out of range) simply matches no rows and returns 0; there
 * is no separate error code. Audit-chain emission stays owned by the
 * legacy cap_table layer until M1-CAPTBL-005's façade migration lands
 * (plan acceptance #2 — audit byte-identity is reserved for that PR).
 * --------------------------------------------------------------------*/
uint32_t cap_handle_revoke_subject(cap_subject_id_t owner_subject) {
  if (!cap_handle_subject_valid(owner_subject)) {
    return 0u;
  }
  uint32_t revoked = 0u;
  for (uint32_t i = 0; i < CAP_HANDLE_TABLE_MAX; ++i) {
    cap_handle_row *row = &g_rows[i];
    if ((row->flags & CAP_HANDLE_FLAG_LIVE) == 0u) {
      continue;
    }
    if (row->owner_subject != owner_subject) {
      continue;
    }
    row->flags = CAP_HANDLE_FLAG_REVOKED;
    row->generation += 1u;
    revoked += 1u;
  }
  return revoked;
}

/* ----------------------------------------------------------------------
 * M1-CAPTBL-004: reserved subtree-revoke stub (issue #241).
 *
 * Reserves the kernel symbol for the M5 ownership-graph cascading
 * deletion (#118) and the M4 broker subtree-revoke (#115). v0 contract
 * is unconditionally CAP_ERR_CAP_INVALID with zero side effects: the
 * parent_handle field on cap_handle_row is reserved-and-zero today, so
 * there is no graph to walk. The real implementation belongs to #118.
 *
 * NOTE: we intentionally do NOT validate `root_handle` here. The return
 * code is the same for every input, and (more importantly) silently
 * succeeding on a "valid" handle would be a footgun once #118 wires up
 * the real walk \u2014 callers might accidentally rely on a no-op. Returning
 * CAP_ERR_CAP_INVALID universally keeps the contract honest.
 * --------------------------------------------------------------------*/
cap_result_t cap_handle_revoke_subtree(cap_handle_t root_handle) {
  (void)root_handle;
  return CAP_ERR_CAP_INVALID;
}
