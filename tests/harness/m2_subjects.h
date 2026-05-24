/**
 * @file m2_subjects.h
 * @brief Backwards-compatibility alias for the M2-only spelling of the
 *        substrate subject-id header.
 *
 * Original M2-only contract lived here (issue #268). Generalised under
 * issue #278 to also cover M3 fs_svc and future substrate slices; the
 * canonical header is now `tests/harness/svc_subjects.h`.
 *
 * Existing slice consumers (`#include "harness/m2_subjects.h"`) keep
 * compiling unchanged via this alias. New code should prefer the
 * canonical header.
 *
 * Issue: #278. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md.
 */

#ifndef SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H
#define SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H

#include "svc_subjects.h"

#endif /* SECUREOS_TESTS_HARNESS_M2_SUBJECTS_H */
