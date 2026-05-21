#ifndef SECUREOS_LAUNCHER_H
#define SECUREOS_LAUNCHER_H

#include <stddef.h>

#include "../cap/capability.h"

/**
 * @file launcher.h
 * @brief Minimal launcher service that mediates app capability grants.
 *
 * Purpose:
 *   Provides the single launcher-mediated entry point for granting and
 *   exercising the console-write capability on behalf of an app subject.
 *   This is the first vertical slice of the zero-trust launcher described
 *   in plans/2026-04-11-console-launcher-capability-slice.md.
 *
 * Invariants:
 *   - Apps registered with the launcher are deny-by-default for
 *     console output.
 *   - Console output is only authorized when the launcher has explicitly
 *     granted CAP_CONSOLE_WRITE to that app via launcher_grant_console_write().
 *   - Read-only inspection helpers (launcher_app_has_console_write) do not
 *     widen access and do not emit audit grant events.
 */

typedef enum {
  LAUNCHER_OK = 0,
  LAUNCHER_ERR_INVALID_APP = 1,
  LAUNCHER_ERR_NOT_REGISTERED = 2,
  LAUNCHER_ERR_DENIED = 3,
  LAUNCHER_ERR_INTERNAL = 4,
} launcher_result_t;

/* Reset launcher state and any registered app grants. Safe for tests. */
void launcher_reset(void);

/*
 * Register an app subject with the launcher so it can later be granted
 * console-write. Registration alone never confers any capability.
 */
launcher_result_t launcher_register_app(cap_subject_id_t app_subject_id);

/*
 * Explicit, launcher-mediated grant of CAP_CONSOLE_WRITE on the app subject.
 * This is the only sanctioned path to widen console output access in this
 * slice. Returns LAUNCHER_ERR_NOT_REGISTERED if the app was never registered.
 */
launcher_result_t launcher_grant_console_write(cap_subject_id_t app_subject_id);

/*
 * Explicit, launcher-mediated revoke. Restores deny-by-default for the app.
 */
launcher_result_t launcher_revoke_console_write(cap_subject_id_t app_subject_id);

/*
 * Sanctioned app output path: routes a console write through the capability
 * gate on behalf of the app subject. Apps that try to bypass this entry
 * point (by writing under their own subject directly) will fail closed
 * because the launcher is the only thing that grants CAP_CONSOLE_WRITE.
 *
 * bytes_written_out may be NULL; on success it is set to the message length.
 */
launcher_result_t launcher_app_console_write(cap_subject_id_t app_subject_id,
                                             const char *message,
                                             size_t *bytes_written_out);

/* Read-only inspection: returns 1 if the app currently holds console-write. */
int launcher_app_has_console_write(cap_subject_id_t app_subject_id);

#endif
