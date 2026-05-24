/**
 * @file svc_subjects.h
 * @brief Canonical subject ids for the SecureOS substrate test suite
 *        (M2 + M3 + future), generalised from the M2-only
 *        `m2_subjects.h` introduced by issue #268.
 *
 * Single source of truth for the substrate subject ids referenced by
 * the M2 console + launcher + HelloApp slices (#268..#271) and the M3
 * fs_svc slices (#278..#281). Issuing them from one header keeps the
 * substrate slices from drifting into mismatched id pickers.
 *
 * Numeric values:
 *   - Kept LOW (single digits) so they stay under
 *     `CAP_TABLE_MAX_SUBJECTS` (8 in v0) — same constraint the M1
 *     module-registry subjects already obey
 *     (`kernel/proc/module_registry.h` SUBJECT_M1_*).
 *   - Distinct from SUBJECT_M1_SENDER/RECEIVER/UNAUTH (5/6/7) so the
 *     M1 IPC demo and the substrate demos can coexist in the same
 *     test phase without subject-id collisions.
 *   - `0` is reserved (the IPC v0 spec requires kernel-stamped
 *     `sender_subject != 0` on receive); never used here.
 *
 * Subject map (frozen under `OS_ABI_VERSION = 0`):
 *   SUBJECT_M2_LAUNCHER    = 1   launcher (M2 mediation point)
 *   SUBJECT_M2_CONSOLE_SVC = 2   in-kernel console service module
 *   SUBJECT_M2_HELLOAPP    = 3   spawned HelloApp under launcher
 *   SUBJECT_M3_FS_SVC      = 4   in-kernel fs service module (#278)
 *
 * Backwards compatibility:
 *   The pre-existing `tests/harness/m2_subjects.h` header still
 *   compiles via a one-line re-include of this file (see that header
 *   for the alias). Slice consumers should prefer the new spelling.
 *
 * Issue: #278 (slice 1 of M3 substrate). Consumed by slices 2/3/4
 * (#279/#280/#281). Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md.
 */

#ifndef SECUREOS_TESTS_HARNESS_SVC_SUBJECTS_H
#define SECUREOS_TESTS_HARNESS_SVC_SUBJECTS_H

#include "../../kernel/cap/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical substrate subject ids. Pinned by tests; do NOT renumber
 * without updating every slice consumer in the same PR.
 */
#define SUBJECT_M2_LAUNCHER    ((cap_subject_id_t)1u)
#define SUBJECT_M2_CONSOLE_SVC ((cap_subject_id_t)2u)
#define SUBJECT_M2_HELLOAPP    ((cap_subject_id_t)3u)
#define SUBJECT_M3_FS_SVC      ((cap_subject_id_t)4u)

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_TESTS_HARNESS_SVC_SUBJECTS_H */
