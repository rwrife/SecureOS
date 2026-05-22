#ifndef SECUREOS_CAP_HANDLE_H
#define SECUREOS_CAP_HANDLE_H

/**
 * @file cap_handle.h
 * @brief M1 kernel capability handle table skeleton (PR-shaped from
 *        plans/2026-05-20-m1-kernel-capability-table.md, execute issue #225).
 *
 * This is the M1-CAPTBL-001 slice: the row struct, the bounded global table,
 * and a minimal grant/revoke/check API on top of it. No behavioural change
 * is introduced for existing `cap_table_*` callers — `cap_table.{c,h}` is
 * untouched in this slice. Handle representation (32-bit packed handle,
 * `cap_gate_check_handle`, process-exit bulk revoke, subtree revoke) are
 * deferred to follow-up execute slices (M1-CAPTBL-002..006).
 *
 * Layout matches the plan exactly:
 *
 *   struct cap_handle_row {
 *     uint16_t        abi_version;
 *     uint16_t        flags;
 *     capability_id_t cap_id;
 *     cap_subject_id_t owner_subject;
 *     cap_subject_id_t granter_subject;
 *     uint32_t        parent_handle;   // reserved, 0 in v0
 *     uint32_t        generation;
 *   };
 *
 * The table is statically allocated (no heap dependency at M1, per
 * docs/CODING_CONVENTIONS.md / #163) and sized at CAP_HANDLE_TABLE_MAX = 64,
 * which is the v0 figure pinned by the plan as sufficient for the M1–M3
 * fixture set.
 */

#include <stdint.h>

#include "capability.h"
#include "cap_table.h"  /* reuses CAP_TABLE_MAX_SUBJECTS as the per-process subject bound */

/* v0 sizing (plan: "Table data structure"). */
#define CAP_HANDLE_TABLE_MAX 64u

/* Row flag bits (plan: "Table data structure"). bit3+ MBZ in v0. */
#define CAP_HANDLE_FLAG_LIVE    (1u << 0)
#define CAP_HANDLE_FLAG_REVOKED (1u << 1)
#define CAP_HANDLE_FLAG_SEALED  (1u << 2)

typedef struct cap_handle_row {
  uint16_t abi_version;
  uint16_t flags;
  capability_id_t cap_id;
  cap_subject_id_t owner_subject;
  cap_subject_id_t granter_subject;
  uint32_t parent_handle;
  uint32_t generation;
} cap_handle_row;

/* Result codes specific to the M1-CAPTBL-001 slice. Reuses the shared
 * cap_result_t enum from capability.h so call sites and tests don't need a
 * second error vocabulary. CAP_ERR_CAP_INVALID doubles as "table full" in
 * this slice; the follow-up handle-representation slice (M1-CAPTBL-002) is
 * the right place to split that out if/when needed. */

void cap_handle_table_init(void);
void cap_handle_table_reset(void);

/* Returns CAP_OK on first grant. Duplicate grants for the same
 * (owner_subject, cap_id) are idempotent and return CAP_OK without bumping
 * generation or allocating a second row (plan §"Mapping the M2 console
 * grants into the table" — `grant` is idempotent on the existing row). */
cap_result_t cap_handle_table_grant(cap_subject_id_t owner_subject,
                                    cap_subject_id_t granter_subject,
                                    capability_id_t cap_id);

/* Revoking a (subject, cap) that has no live row returns CAP_ERR_MISSING
 * (plan §"Revocation hooks": revoke has a clear error contract; we lift the
 * existing `cap_check`-style missing marker so audit/log behaviour stays
 * uniform). On success, clears LIVE, sets REVOKED, bumps generation, and
 * returns CAP_OK. The row is retained (slot is not freed in v0) so the
 * generation counter is observable for the M1-CAPTBL-002 staleness check. */
cap_result_t cap_handle_table_revoke(cap_subject_id_t owner_subject,
                                     capability_id_t cap_id);

/* CAP_OK if a live row exists for (owner_subject, cap_id); CAP_ERR_MISSING
 * otherwise (after revoke, or never granted). Bounds errors return
 * CAP_ERR_SUBJECT_INVALID / CAP_ERR_CAP_INVALID matching the existing
 * cap_table contract. */
cap_result_t cap_handle_table_check(cap_subject_id_t owner_subject,
                                    capability_id_t cap_id);

/* Test/inspection helper: number of currently-live rows in the table. Used
 * by the unit tests to assert idempotency and table-full behaviour without
 * exposing the row array itself. */
uint32_t cap_handle_table_live_count(void);

#endif
