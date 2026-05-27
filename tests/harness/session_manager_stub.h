/**
 * @file session_manager_stub.h
 * @brief Tiny host-test stand-in for kernel/core/session_manager.c.
 *
 * Purpose:
 *   `kernel/svc/broker_svc.c` now references two session_manager
 *   symbols at link time (M5-SUBSTRATE-005b, issue #350 step 3.5):
 *     - session_manager_first_session_for_subject()
 *     - session_manager_destroy()
 *
 *   The full session_manager.c implementation pulls in scheduler,
 *   kheap, vfb_font, serial_hal, console, ctx_switch \u2014 way too much
 *   surface for the existing pure-cap-handle / pure-broker host
 *   fixtures (`broker_svc_cascade_revokes_minted_handle_test`,
 *   `broker_svc_delete_owner_authority_check_test`).
 *
 *   This stub provides a minimal in-memory session table that:
 *     - Satisfies the two symbols broker_svc.c needs at link time.
 *     - Defaults to "no sessions exist" \u2014 so existing tests that
 *       don't care see step 3.5 as a no-op zero-iteration loop.
 *     - Exposes test hooks (sm_stub_inject / sm_stub_destroyed) so
 *       the M5-SUBSTRATE-005b cascade test can inject sessions
 *       owned by a subject and assert the cascade tore them down.
 *
 * Pure host. Not linked into the kernel image. Mirrors the
 * `tests/harness/svc_subjects.{c,h}` convention for host-side
 * fixtures shared across multiple test drivers.
 *
 * Issue: #350. Plan: plans/2026-05-26-m5-wm-cascade-on-substrate.md
 * slice 005b.
 */
#ifndef SECUREOS_TESTS_HARNESS_SESSION_MANAGER_STUB_H
#define SECUREOS_TESTS_HARNESS_SESSION_MANAGER_STUB_H

#include "../../kernel/cap/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Test bound. Larger than kernel's SESSION_MAX so a destroy_count
 * overflow can never be the proximate cause of a false PASS. */
#define SM_STUB_MAX 8u

/* Reset the in-test session table to empty. Call at the top of every
 * fixture that touches broker_svc cascade paths. */
void sm_stub_reset(void);

/* Inject a session owned by `subject` into the stub table. On success
 * returns 0 and writes the assigned session id to *out_sid; returns
 * -1 if the stub table is full. */
int  sm_stub_inject(cap_subject_id_t subject, unsigned int *out_sid);

/* Returns 1 if `sid` was injected and not yet destroyed, else 0. */
int  sm_stub_in_use(unsigned int sid);

/* Total number of times the stub's session_manager_destroy() shim
 * has been invoked since the last sm_stub_reset(). */
unsigned int sm_stub_destroy_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_TESTS_HARNESS_SESSION_MANAGER_STUB_H */
