#ifndef SECUREOS_LAUNCHER_FS_H
#define SECUREOS_LAUNCHER_FS_H

#include <stddef.h>

#include "../cap/capability.h"

/**
 * @file launcher_fs.h
 * @brief Launcher-mediated filesystem capability slice with faux ephemeral FS.
 *
 * Purpose:
 *   Implements the next zero-trust vertical slice described in
 *   plans/2026-04-16-filesystem-service-faux-fs.md.  Apps must be
 *   registered with the launcher to obtain CAP_FS_READ / CAP_FS_WRITE,
 *   and each app is bound to one of two storage modes:
 *     - LAUNCHER_FS_MODE_PERSISTENT: state survives app relaunch.
 *     - LAUNCHER_FS_MODE_EPHEMERAL:  state is cleared on app relaunch.
 *
 *   Direct file I/O without launcher mediation fails closed because the
 *   launcher is the only sanctioned path that grants the FS capabilities.
 *
 * Invariants:
 *   - Registered apps are deny-by-default for read and write.
 *   - launcher_fs_grant_* / launcher_fs_revoke_* are the only sanctioned ways to widen access.
 *   - Ephemeral apps do not retain any data across launcher_fs_app_relaunch().
 *   - Persistent apps retain data across launcher_fs_app_relaunch() until
 *     launcher_fs_reset() (or explicit unlink) wipes it.
 */

typedef enum {
  LAUNCHER_FS_OK = 0,
  LAUNCHER_FS_ERR_INVALID_APP = 1,
  LAUNCHER_FS_ERR_NOT_REGISTERED = 2,
  LAUNCHER_FS_ERR_DENIED = 3,
  LAUNCHER_FS_ERR_INVALID_ARG = 4,
  LAUNCHER_FS_ERR_NO_SPACE = 5,
  LAUNCHER_FS_ERR_NOT_FOUND = 6,
  LAUNCHER_FS_ERR_INTERNAL = 7,
} launcher_fs_result_t;

typedef enum {
  LAUNCHER_FS_MODE_PERSISTENT = 0,
  LAUNCHER_FS_MODE_EPHEMERAL = 1,
} launcher_fs_mode_t;

/* Reset all launcher-fs state, revoking grants and wiping all storage. */
void launcher_fs_reset(void);

/*
 * Register an app subject with the launcher in the requested storage mode.
 * Re-registering the same subject in the same mode is idempotent.
 * Re-registering with a different mode is rejected to prevent silent
 * privilege/lifecycle changes.
 */
launcher_fs_result_t launcher_fs_register_app(cap_subject_id_t app_subject_id,
                                              launcher_fs_mode_t mode);

/* Explicit, launcher-mediated grants. */
launcher_fs_result_t launcher_fs_grant_read(cap_subject_id_t app_subject_id);
launcher_fs_result_t launcher_fs_grant_write(cap_subject_id_t app_subject_id);
launcher_fs_result_t launcher_fs_revoke_read(cap_subject_id_t app_subject_id);
launcher_fs_result_t launcher_fs_revoke_write(cap_subject_id_t app_subject_id);

/*
 * Sanctioned write/read entry points. They route through cap_check() so the
 * deny-by-default policy is enforced even if a caller forges its own subject
 * id, because the launcher is the only thing that grants CAP_FS_*.
 */
launcher_fs_result_t launcher_fs_app_write(cap_subject_id_t app_subject_id,
                                           const char *path,
                                           const char *content);
launcher_fs_result_t launcher_fs_app_read(cap_subject_id_t app_subject_id,
                                          const char *path,
                                          char *out_buffer,
                                          size_t out_buffer_size,
                                          size_t *out_len);

/*
 * Notify the launcher that the app has been relaunched. Ephemeral storage
 * for that app is wiped; persistent storage is preserved. Capability grants
 * are intentionally NOT preserved across relaunch and must be re-issued by
 * the launcher, mirroring real launch-time mediation.
 */
launcher_fs_result_t launcher_fs_app_relaunch(cap_subject_id_t app_subject_id);

/* Read-only inspection helpers. They never widen access. */
int launcher_fs_app_has_read(cap_subject_id_t app_subject_id);
int launcher_fs_app_has_write(cap_subject_id_t app_subject_id);
launcher_fs_mode_t launcher_fs_app_mode(cap_subject_id_t app_subject_id);

#endif
