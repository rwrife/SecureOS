/**
 * @file launcher.c
 * @brief Launcher-mediated capability slice for console-write.
 *
 * Purpose:
 *   Implements the minimal launcher described in
 *   plans/2026-04-11-console-launcher-capability-slice.md. Apps must be
 *   registered with the launcher, and CAP_CONSOLE_WRITE must be granted via
 *   the launcher before any console output is permitted. Read-only inspection
 *   never widens access.
 *
 * Interactions:
 *   - cap_table.c: launcher_grant_console_write/launcher_revoke_console_write
 *     update the capability table on behalf of the app subject.
 *   - cap_gate.c: launcher_app_console_write routes through
 *     cap_console_write_gate so the deny-by-default policy is enforced.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and reset at
 *   boot or by tests via launcher_reset().
 */

#include "launcher.h"

#include "../cap/cap_gate.h"
#include "../cap/cap_table.h"

#define LAUNCHER_MAX_APPS CAP_TABLE_MAX_SUBJECTS

typedef struct {
  int used;
  cap_subject_id_t subject_id;
} launcher_app_entry_t;

static launcher_app_entry_t launcher_apps[LAUNCHER_MAX_APPS];

static launcher_app_entry_t *launcher_find_entry(cap_subject_id_t app_subject_id) {
  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (launcher_apps[i].used && launcher_apps[i].subject_id == app_subject_id) {
      return &launcher_apps[i];
    }
  }
  return 0;
}

static int launcher_subject_in_range(cap_subject_id_t app_subject_id) {
  return app_subject_id < CAP_TABLE_MAX_SUBJECTS;
}

void launcher_reset(void) {
  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (launcher_apps[i].used) {
      /* Best-effort revoke so a reset cannot leave stale capabilities. */
      (void)cap_table_revoke(launcher_apps[i].subject_id, CAP_CONSOLE_WRITE);
    }
    launcher_apps[i].used = 0;
    launcher_apps[i].subject_id = 0u;
  }
}

launcher_result_t launcher_register_app(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) != 0) {
    return LAUNCHER_OK;
  }

  for (size_t i = 0; i < LAUNCHER_MAX_APPS; ++i) {
    if (!launcher_apps[i].used) {
      launcher_apps[i].used = 1;
      launcher_apps[i].subject_id = app_subject_id;
      return LAUNCHER_OK;
    }
  }

  return LAUNCHER_ERR_INTERNAL;
}

launcher_result_t launcher_grant_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  if (cap_table_grant(app_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return LAUNCHER_ERR_INTERNAL;
  }

  return LAUNCHER_OK;
}

launcher_result_t launcher_revoke_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  if (cap_table_revoke(app_subject_id, CAP_CONSOLE_WRITE) != CAP_OK) {
    return LAUNCHER_ERR_INTERNAL;
  }

  return LAUNCHER_OK;
}

launcher_result_t launcher_app_console_write(cap_subject_id_t app_subject_id,
                                             const char *message,
                                             size_t *bytes_written_out) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return LAUNCHER_ERR_INVALID_APP;
  }

  if (launcher_find_entry(app_subject_id) == 0) {
    return LAUNCHER_ERR_NOT_REGISTERED;
  }

  cap_result_t gate_result = cap_console_write_gate(app_subject_id, message, bytes_written_out);
  if (gate_result != CAP_OK) {
    return LAUNCHER_ERR_DENIED;
  }

  return LAUNCHER_OK;
}

int launcher_app_has_console_write(cap_subject_id_t app_subject_id) {
  if (!launcher_subject_in_range(app_subject_id)) {
    return 0;
  }
  if (launcher_find_entry(app_subject_id) == 0) {
    return 0;
  }
  return cap_table_check(app_subject_id, CAP_CONSOLE_WRITE) == CAP_OK;
}
