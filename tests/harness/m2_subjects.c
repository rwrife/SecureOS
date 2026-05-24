/**
 * @file m2_subjects.c
 * @brief Compile-time assertions for the canonical M2 substrate subject ids.
 *
 * Header-only-ish: the values themselves live in `m2_subjects.h`.
 * This translation unit only exists to:
 *
 *   1. Statically assert the ids are in the legal cap-table range
 *      (mirrors the SUBJECT_M1_* range guard in
 *      `kernel/proc/module_registry.h`).
 *   2. Assert all three ids are distinct from one another and from the
 *      reserved `0` so a future renumbering can't silently re-alias.
 *   3. Give the build a non-empty object file when this header is
 *      consumed via a `.c,h` pair (per the slice-1 / issue #268 spec).
 *
 * No runtime function is exported. Adding behaviour here is out of
 * scope; per-slice helpers belong in their own translation units.
 *
 * Issue: #268. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 1.
 */

#include "m2_subjects.h"

/* CAP_TABLE_MAX_SUBJECTS is 8 in v0 (see comment in
 * kernel/proc/module_registry.h on the same constraint). Subject ids
 * must be strictly less than that bound; the actual constant lives in
 * kernel/cap/cap_table.h. Replicating it here would create a second
 * source of truth, so the static_assert below uses the literal `8u`
 * with a NOTE: if cap_table grows past 8, update this guard in the
 * same PR that updates `kernel/cap/cap_table.h`. */
_Static_assert(SUBJECT_M2_LAUNCHER < 8u,
               "SUBJECT_M2_LAUNCHER must fit under CAP_TABLE_MAX_SUBJECTS");
_Static_assert(SUBJECT_M2_CONSOLE_SVC < 8u,
               "SUBJECT_M2_CONSOLE_SVC must fit under CAP_TABLE_MAX_SUBJECTS");
_Static_assert(SUBJECT_M2_HELLOAPP < 8u,
               "SUBJECT_M2_HELLOAPP must fit under CAP_TABLE_MAX_SUBJECTS");

_Static_assert(SUBJECT_M2_LAUNCHER != 0u
               && SUBJECT_M2_CONSOLE_SVC != 0u
               && SUBJECT_M2_HELLOAPP != 0u,
               "M2 substrate subject ids must not collide with the "
               "reserved 0 used by ipc_msg_v0 to signal "
               "unstamped/invalid sender_subject");

_Static_assert(SUBJECT_M2_LAUNCHER != SUBJECT_M2_CONSOLE_SVC
               && SUBJECT_M2_LAUNCHER != SUBJECT_M2_HELLOAPP
               && SUBJECT_M2_CONSOLE_SVC != SUBJECT_M2_HELLOAPP,
               "M2 substrate subject ids must be pairwise distinct");

/* Keep at least one external symbol in the .o so linkers that strip
 * empty TUs don't drop the static_asserts above on an LTO build. */
const unsigned int m2_subjects_compile_marker = 0xC0DEC0DEu;
