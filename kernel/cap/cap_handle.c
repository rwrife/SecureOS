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
 * (#150). The literal lives in user/include/secureos_abi.h once that
 * surface is finalised; for the M1-CAPTBL-001 skeleton we pin a local
 * constant equal to 0 so each row carries the v0 marker today and the
 * follow-up slice can flip this to an `#include` without disturbing any
 * persisted artefact. */
#define CAP_HANDLE_ABI_VERSION_V0 0u

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
