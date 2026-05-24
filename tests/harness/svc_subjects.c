/**
 * @file svc_subjects.c
 * @brief Compile-time assertions for the canonical substrate subject ids.
 *
 * Values live in `svc_subjects.h`. This translation unit only:
 *
 *   1. Statically asserts the ids are in the legal cap-table range
 *      (mirrors the SUBJECT_M1_* range guard in
 *      `kernel/proc/module_registry.h`).
 *   2. Asserts all ids are distinct from one another and from the
 *      reserved `0` so a future renumbering can't silently re-alias.
 *   3. Gives the build a non-empty object file when this header is
 *      consumed via a `.c,h` pair.
 *
 * Issue: #278. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md.
 */

#include "svc_subjects.h"

/* CAP_TABLE_MAX_SUBJECTS is 8 in v0 (see comment in
 * kernel/proc/module_registry.h on the same constraint). Subject ids
 * must be strictly less than that bound; replicating the literal `8u`
 * here mirrors the m2_subjects.c rationale (single source of truth
 * lives in kernel/cap/cap_table.h; this guard fires loudly if cap_table
 * grows past 8 without updating this file in the same PR). */
_Static_assert(SUBJECT_M2_LAUNCHER < 8u,
               "SUBJECT_M2_LAUNCHER must fit under CAP_TABLE_MAX_SUBJECTS");
_Static_assert(SUBJECT_M2_CONSOLE_SVC < 8u,
               "SUBJECT_M2_CONSOLE_SVC must fit under CAP_TABLE_MAX_SUBJECTS");
_Static_assert(SUBJECT_M2_HELLOAPP < 8u,
               "SUBJECT_M2_HELLOAPP must fit under CAP_TABLE_MAX_SUBJECTS");
_Static_assert(SUBJECT_M3_FS_SVC < 8u,
               "SUBJECT_M3_FS_SVC must fit under CAP_TABLE_MAX_SUBJECTS");

_Static_assert(SUBJECT_M2_LAUNCHER != 0u
               && SUBJECT_M2_CONSOLE_SVC != 0u
               && SUBJECT_M2_HELLOAPP != 0u
               && SUBJECT_M3_FS_SVC != 0u,
               "substrate subject ids must not collide with the "
               "reserved 0 used by ipc_msg_v0 to signal "
               "unstamped/invalid sender_subject");

_Static_assert(SUBJECT_M2_LAUNCHER != SUBJECT_M2_CONSOLE_SVC
               && SUBJECT_M2_LAUNCHER != SUBJECT_M2_HELLOAPP
               && SUBJECT_M2_LAUNCHER != SUBJECT_M3_FS_SVC
               && SUBJECT_M2_CONSOLE_SVC != SUBJECT_M2_HELLOAPP
               && SUBJECT_M2_CONSOLE_SVC != SUBJECT_M3_FS_SVC
               && SUBJECT_M2_HELLOAPP != SUBJECT_M3_FS_SVC,
               "substrate subject ids must be pairwise distinct");

/* Keep at least one external symbol in the .o so linkers that strip
 * empty TUs don't drop the static_asserts above on an LTO build. */
const unsigned int svc_subjects_compile_marker = 0xC0DEC0DFu;
