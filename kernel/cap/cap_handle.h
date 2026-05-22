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
#include "../../user/include/secureos_abi.h"

/* v0 sizing (plan: "Table data structure"). */
#define CAP_HANDLE_TABLE_MAX 64u

/*
 * 32-bit packed capability handle (M1-CAPTBL-002, issue #233).
 *
 * The handle is the wire-stable identifier that the future M1 IPC primitive
 * (#180/#185) and the syscall-entry trampoline (#192) will pass across the
 * kernel/user boundary. Layout is frozen against OS_ABI_VERSION=0 (#150)
 * and documented in docs/abi/capabilities.md.
 *
 *   bits  0..15  : slot index into the global cap_handle_row table
 *                  (0 .. CAP_HANDLE_TABLE_MAX-1).
 *   bits 16..29  : generation low-14-bits (wrap-safe; the underlying
 *                  row.generation is 32-bit, but only the low 14 are
 *                  reified on the wire — collisions only after 16384
 *                  revocations of the same slot, which exceeds the M1
 *                  bound).
 *   bits 30..31  : tag = 0b01 ("kernel capability handle"). Reserved tags:
 *                  0b00 = invalid/null, 0b10/0b11 = future kinds (file
 *                  handles, IPC ports, broker tickets).
 *
 * A handle whose tag bits are not 0b01 is rejected by cap_gate_check_handle
 * with CAP_ERR_CAP_INVALID. A handle whose slot index lies outside the
 * table is rejected with CAP_ERR_CAP_INVALID. A handle whose generation
 * field does not match the live row's generation low-14-bits is rejected
 * with CAP_ERR_MISSING (stale handle, post-revoke).
 */
typedef uint32_t cap_handle_t;

#define CAP_HANDLE_NULL          ((cap_handle_t)0u)

#define CAP_HANDLE_SLOT_BITS     16u
#define CAP_HANDLE_GEN_BITS      14u
#define CAP_HANDLE_TAG_BITS      2u

#define CAP_HANDLE_SLOT_SHIFT    0u
#define CAP_HANDLE_GEN_SHIFT     CAP_HANDLE_SLOT_BITS
#define CAP_HANDLE_TAG_SHIFT     (CAP_HANDLE_SLOT_BITS + CAP_HANDLE_GEN_BITS)

#define CAP_HANDLE_SLOT_MASK     ((cap_handle_t)((1u << CAP_HANDLE_SLOT_BITS) - 1u))
#define CAP_HANDLE_GEN_MASK      ((cap_handle_t)((1u << CAP_HANDLE_GEN_BITS) - 1u))
#define CAP_HANDLE_TAG_MASK      ((cap_handle_t)((1u << CAP_HANDLE_TAG_BITS) - 1u))

#define CAP_HANDLE_TAG_KERNEL    ((cap_handle_t)0x1u)  /* 0b01 */

/* Compile-time invariants pinning the layout to OS_ABI_VERSION=0 (#150).
 * Any change here is a v0->v1 ABI break and must follow §7 of the roadmap. */
#if (CAP_HANDLE_SLOT_BITS + CAP_HANDLE_GEN_BITS + CAP_HANDLE_TAG_BITS) != 32u
#error "cap_handle_t layout must consume exactly 32 bits at OS_ABI_VERSION=0"
#endif
#if OS_ABI_VERSION != 0
#error "cap_handle_t layout is frozen at OS_ABI_VERSION=0; bump requires v1 audit"
#endif

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

/* ----------------------------------------------------------------------
 * M1-CAPTBL-002: 32-bit packed handle API (issue #233).
 *
 * These functions sit alongside the existing cap_handle_table_* surface and
 * are the entry points that the M1 IPC primitive (#180/#185) and future
 * syscall trampoline (#192) will consume. They do NOT yet replace the
 * thin-bitset cap_table_* API — that façade migration is reserved for
 * M1-CAPTBL-005 so the audit byte-identity gate can guard it.
 * --------------------------------------------------------------------*/

/*
 * Grant (subject, cap_id) and return the wire-stable 32-bit handle that
 * authorizes it. Caller is treated as the granter (one-arg form per the
 * issue body). Idempotent: a second grant for the same live (subject, cap)
 * returns the SAME handle (slot+generation unchanged); the underlying row
 * is not duplicated, mirroring cap_handle_table_grant's contract.
 *
 * Returns CAP_HANDLE_NULL on any error (bad subject, bad cap_id, or table
 * full). NULL handles always fail cap_gate_check_handle.
 */
cap_handle_t cap_handle_grant(cap_subject_id_t owner_subject,
                              capability_id_t cap_id);

/*
 * Verify a handle resolves to a live row for `expected_cap` and return
 * true; otherwise emit the canonical capability-denied marker to the
 * caller-provided sink and return false.
 *
 * Failure modes:
 *   - tag bits != CAP_HANDLE_TAG_KERNEL  -> CAP_ERR_CAP_INVALID
 *   - slot index out of bounds           -> CAP_ERR_CAP_INVALID
 *   - row not live (never granted / row reused) -> CAP_ERR_MISSING
 *   - generation mismatch (stale / post-revoke) -> CAP_ERR_MISSING
 *   - row's cap_id != expected_cap       -> CAP_ERR_CAP_INVALID
 *
 * The detailed cap_result_t is available via cap_gate_check_handle_result()
 * for tests and for the syscall trampoline that will translate it to
 * OS_STATUS_DENIED. The bool form is what plan §"Acceptance criteria" #4
 * pins.
 */
int cap_gate_check_handle(cap_handle_t handle, capability_id_t expected_cap);

/*
 * Detailed-result form. Same checks as cap_gate_check_handle but returns
 * the cap_result_t code, so callers can distinguish stale (CAP_ERR_MISSING)
 * from malformed (CAP_ERR_CAP_INVALID). Pure check — no audit emission, no
 * table mutation.
 */
cap_result_t cap_gate_check_handle_result(cap_handle_t handle,
                                          capability_id_t expected_cap);

/*
 * Revoke a handle by bumping its row's generation, so the same numeric
 * handle now denies. Idempotent: revoking an already-revoked or unknown
 * handle is a no-op (returns CAP_ERR_MISSING / CAP_ERR_CAP_INVALID). The
 * row is NOT freed in v0 — generation persists so prior handles stay
 * permanently stale (plan §"Handle representation").
 *
 * Mirrors cap_handle_table_revoke's contract for the (subject, cap) pair
 * the handle authorizes; the two paths converge on the same row.
 */
cap_result_t cap_handle_revoke(cap_handle_t handle);

/*
 * Bulk-revoke every live row owned by `owner_subject` (M1-CAPTBL-003,
 * issue #239). Single pass over the global table; each matching live
 * row is transitioned LIVE -> REVOKED with its generation bumped, so
 * every handle previously issued for it now fails `cap_gate_check_handle`
 * with CAP_ERR_MISSING. Returns the number of rows actually revoked
 * (0 if the subject held none or if `owner_subject` is out of range).
 *
 * Best-effort by contract: there is no error code. Callers that need a
 * detailed result use `cap_handle_revoke` on individual handles instead.
 *
 * Called by `kernel/proc/process.c::process_destroy` so process exit
 * authoritatively stales every capability handle the process owned.
 */
uint32_t cap_handle_revoke_subject(cap_subject_id_t owner_subject);

/*
 * Reserved symbol for M5 ownership-graph cascading deletion (#118) and
 * M4 broker subtree-revoke (#115). Frozen so downstream work can compile
 * against this symbol while the underlying cap_handle layer is still
 * small and easy to reason about (M1-CAPTBL-004, issue #241, plan #197).
 *
 * v0 contract: unconditionally returns CAP_ERR_CAP_INVALID. No table
 * mutation, no audit emission, no side effects whatsoever. Callers MUST
 * NOT depend on subtree-revoke semantics yet — the parent_handle field
 * on cap_handle_row is reserved-and-zero in v0, so there is no graph to
 * walk. The real implementation is owned by #118 and may change error
 * codes and semantics; this stub only reserves the symbol shape.
 */
cap_result_t cap_handle_revoke_subtree(cap_handle_t root_handle);

/* Test helper: pack/unpack the components of a handle. Exposed because the
 * ABI-freeze unit test exercises the layout directly. */
cap_handle_t cap_handle_pack(uint16_t slot, uint16_t generation_low14,
                             uint8_t tag);
uint16_t cap_handle_slot(cap_handle_t handle);
uint16_t cap_handle_generation(cap_handle_t handle);
uint8_t cap_handle_tag(cap_handle_t handle);

#endif
