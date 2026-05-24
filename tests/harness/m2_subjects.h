/**
 * @file m2_subjects.h
 * @brief Canonical subject ids for the M2-on-M1 substrate test suite.
 *
 * Single source of truth for the three subject ids the slice-1 / -2 /
 * -3 / -4 PRs reference (console service, launcher, HelloApp). Issuing
 * them from one header keeps the three substrate slices from drifting
 * into mismatched id pickers.
 *
 * Numeric values:
 *   - Kept LOW (single digits) so they stay under
 *     `CAP_TABLE_MAX_SUBJECTS` (8 in v0) — same constraint the M1
 *     module-registry subjects already obey
 *     (`kernel/proc/module_registry.h` SUBJECT_M1_*).
 *   - Distinct from SUBJECT_M1_SENDER/RECEIVER/UNAUTH (5/6/7) so the
 *     M1 IPC demo and the M2 substrate demo can coexist in the same
 *     test phase without subject-id collisions.
 *   - `0` is reserved (the IPC v0 spec requires kernel-stamped
 *     `sender_subject != 0` on receive); never used here.
 *
 * Subject map (frozen under `OS_ABI_VERSION = 0`):
 *   SUBJECT_M2_LAUNCHER    = 1   launcher (M2 mediation point)
 *   SUBJECT_M2_CONSOLE_SVC = 2   in-kernel console service module
 *   SUBJECT_M2_HELLOAPP    = 3   spawned HelloApp under launcher
 *                                (placeholder until slice 3 lands)
 *
 * Issue: #268 (slice 1). Consumed by slices 2 / 3 / 4 (#269 / #270 /
 * #271). Plan: plans/2026-05-23-m2-on-m1-substrate.md §"Risks and
 * explicit assumptions" — shared helper to keep each new test under
 * ~120 LoC.
 */

#ifndef SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H
#define SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H

#include "../../kernel/cap/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonical M2 substrate subject ids. Pinned by tests; do NOT
 * renumber without updating every slice-1..4 consumer in the same PR.
 */
#define SUBJECT_M2_LAUNCHER    ((cap_subject_id_t)1u)
#define SUBJECT_M2_CONSOLE_SVC ((cap_subject_id_t)2u)
#define SUBJECT_M2_HELLOAPP    ((cap_subject_id_t)3u)

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H */
