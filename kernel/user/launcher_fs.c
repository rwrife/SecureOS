/**
 * @file launcher_fs.c
 * @brief Launcher-mediated filesystem capability slice.
 *
 * Purpose:
 *   Implements the launcher-mediated FS slice described in
 *   plans/2026-04-16-filesystem-service-faux-fs.md. Provides a minimal
 *   per-app file namespace with two storage modes:
 *     - LAUNCHER_FS_MODE_PERSISTENT (real ramfs-backed app state)
 *     - LAUNCHER_FS_MODE_EPHEMERAL  (faux storage cleared on relaunch)
 *
 *   All read/write operations route through cap_table_check() so the
 *   default-deny capability policy is enforced. The launcher is the only
 *   sanctioned path that grants CAP_FS_READ / CAP_FS_WRITE for these
 *   subjects, so attempts to bypass the launcher fail closed.
 *
 * Interactions:
 *   - cap_table.c: launcher_fs_grant_x and launcher_fs_revoke_x, plus the relaunch flow,
 *     update the capability table on behalf of the app subject.
 *   - capability.h: uses CAP_FS_READ and CAP_FS_WRITE.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and reset at
 *   boot or by tests via launcher_fs_reset().
 */

#include "launcher_fs.h"

#include "../cap/cap_table.h"
#include "../cap/capability.h"

#define LAUNCHER_FS_MAX_APPS CAP_TABLE_MAX_SUBJECTS
#define LAUNCHER_FS_MAX_FILES_PER_APP 4
#define LAUNCHER_FS_MAX_PATH 32
#define LAUNCHER_FS_MAX_CONTENT 64

typedef struct {
  int used;
  char path[LAUNCHER_FS_MAX_PATH];
  char content[LAUNCHER_FS_MAX_CONTENT];
  size_t content_len;
} launcher_fs_file_t;

typedef struct {
  int used;
  cap_subject_id_t subject_id;
  launcher_fs_mode_t mode;
  launcher_fs_file_t files[LAUNCHER_FS_MAX_FILES_PER_APP];
} launcher_fs_app_t;

static launcher_fs_app_t launcher_fs_apps[LAUNCHER_FS_MAX_APPS];

static int launcher_fs_subject_in_range(cap_subject_id_t app_subject_id) {
  return app_subject_id < CAP_TABLE_MAX_SUBJECTS;
}

static launcher_fs_app_t *launcher_fs_find(cap_subject_id_t app_subject_id) {
  for (size_t i = 0; i < LAUNCHER_FS_MAX_APPS; ++i) {
    if (launcher_fs_apps[i].used &&
        launcher_fs_apps[i].subject_id == app_subject_id) {
      return &launcher_fs_apps[i];
    }
  }
  return 0;
}

static int launcher_fs_str_eq(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i]) {
      return 0;
    }
    ++i;
  }
  return a[i] == '\0' && b[i] == '\0';
}

static size_t launcher_fs_strlen(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

static void launcher_fs_clear_files(launcher_fs_app_t *app) {
  for (size_t i = 0; i < LAUNCHER_FS_MAX_FILES_PER_APP; ++i) {
    app->files[i].used = 0;
    app->files[i].content_len = 0;
    app->files[i].path[0] = '\0';
    app->files[i].content[0] = '\0';
  }
}

void launcher_fs_reset(void) {
  for (size_t i = 0; i < LAUNCHER_FS_MAX_APPS; ++i) {
    if (launcher_fs_apps[i].used) {
      /* Best-effort revoke so reset cannot leave stale capabilities. */
      (void)cap_table_revoke(launcher_fs_apps[i].subject_id, CAP_FS_READ);
      (void)cap_table_revoke(launcher_fs_apps[i].subject_id, CAP_FS_WRITE);
    }
    launcher_fs_apps[i].used = 0;
    launcher_fs_apps[i].subject_id = 0u;
    launcher_fs_apps[i].mode = LAUNCHER_FS_MODE_PERSISTENT;
    launcher_fs_clear_files(&launcher_fs_apps[i]);
  }
}

launcher_fs_result_t launcher_fs_register_app(cap_subject_id_t app_subject_id,
                                              launcher_fs_mode_t mode) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  if (mode != LAUNCHER_FS_MODE_PERSISTENT && mode != LAUNCHER_FS_MODE_EPHEMERAL) {
    return LAUNCHER_FS_ERR_INVALID_ARG;
  }

  launcher_fs_app_t *existing = launcher_fs_find(app_subject_id);
  if (existing != 0) {
    if (existing->mode != mode) {
      /* Refuse silent mode flips that would change persistence semantics. */
      return LAUNCHER_FS_ERR_INVALID_ARG;
    }
    return LAUNCHER_FS_OK;
  }

  for (size_t i = 0; i < LAUNCHER_FS_MAX_APPS; ++i) {
    if (!launcher_fs_apps[i].used) {
      launcher_fs_apps[i].used = 1;
      launcher_fs_apps[i].subject_id = app_subject_id;
      launcher_fs_apps[i].mode = mode;
      launcher_fs_clear_files(&launcher_fs_apps[i]);
      return LAUNCHER_FS_OK;
    }
  }
  return LAUNCHER_FS_ERR_INTERNAL;
}

static launcher_fs_result_t launcher_fs_grant(cap_subject_id_t app_subject_id,
                                              capability_id_t cap) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  if (launcher_fs_find(app_subject_id) == 0) {
    return LAUNCHER_FS_ERR_NOT_REGISTERED;
  }
  if (cap_table_grant(app_subject_id, cap) != CAP_OK) {
    return LAUNCHER_FS_ERR_INTERNAL;
  }
  return LAUNCHER_FS_OK;
}

static launcher_fs_result_t launcher_fs_revoke(cap_subject_id_t app_subject_id,
                                               capability_id_t cap) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  if (launcher_fs_find(app_subject_id) == 0) {
    return LAUNCHER_FS_ERR_NOT_REGISTERED;
  }
  if (cap_table_revoke(app_subject_id, cap) != CAP_OK) {
    return LAUNCHER_FS_ERR_INTERNAL;
  }
  return LAUNCHER_FS_OK;
}

launcher_fs_result_t launcher_fs_grant_read(cap_subject_id_t app_subject_id) {
  return launcher_fs_grant(app_subject_id, CAP_FS_READ);
}

launcher_fs_result_t launcher_fs_grant_write(cap_subject_id_t app_subject_id) {
  return launcher_fs_grant(app_subject_id, CAP_FS_WRITE);
}

launcher_fs_result_t launcher_fs_revoke_read(cap_subject_id_t app_subject_id) {
  return launcher_fs_revoke(app_subject_id, CAP_FS_READ);
}

launcher_fs_result_t launcher_fs_revoke_write(cap_subject_id_t app_subject_id) {
  return launcher_fs_revoke(app_subject_id, CAP_FS_WRITE);
}

static launcher_fs_file_t *launcher_fs_lookup(launcher_fs_app_t *app,
                                              const char *path) {
  for (size_t i = 0; i < LAUNCHER_FS_MAX_FILES_PER_APP; ++i) {
    if (app->files[i].used && launcher_fs_str_eq(app->files[i].path, path)) {
      return &app->files[i];
    }
  }
  return 0;
}

static launcher_fs_file_t *launcher_fs_alloc_slot(launcher_fs_app_t *app) {
  for (size_t i = 0; i < LAUNCHER_FS_MAX_FILES_PER_APP; ++i) {
    if (!app->files[i].used) {
      return &app->files[i];
    }
  }
  return 0;
}

launcher_fs_result_t launcher_fs_app_write(cap_subject_id_t app_subject_id,
                                           const char *path,
                                           const char *content) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  if (path == 0 || content == 0) {
    return LAUNCHER_FS_ERR_INVALID_ARG;
  }

  launcher_fs_app_t *app = launcher_fs_find(app_subject_id);
  if (app == 0) {
    return LAUNCHER_FS_ERR_NOT_REGISTERED;
  }

  if (cap_table_check(app_subject_id, CAP_FS_WRITE) != CAP_OK) {
    /* Issue #311: emit audit-deny record on the persistent-write deny
     * path. The fs_svc deny is a CHECK outcome (no grant attempted), so
     * we use CAP_AUDIT_OP_CHECK with the missing-cap result. */
    cap_audit_emit(CAP_AUDIT_OP_CHECK,
                   app_subject_id,
                   app_subject_id,
                   CAP_FS_WRITE,
                   CAP_ERR_MISSING);
    return LAUNCHER_FS_ERR_DENIED;
  }

  size_t path_len = launcher_fs_strlen(path);
  size_t content_len = launcher_fs_strlen(content);
  if (path_len == 0 || path_len >= LAUNCHER_FS_MAX_PATH) {
    return LAUNCHER_FS_ERR_INVALID_ARG;
  }
  if (content_len >= LAUNCHER_FS_MAX_CONTENT) {
    return LAUNCHER_FS_ERR_NO_SPACE;
  }

  launcher_fs_file_t *file = launcher_fs_lookup(app, path);
  if (file == 0) {
    file = launcher_fs_alloc_slot(app);
    if (file == 0) {
      return LAUNCHER_FS_ERR_NO_SPACE;
    }
    file->used = 1;
    for (size_t i = 0; i < path_len; ++i) {
      file->path[i] = path[i];
    }
    file->path[path_len] = '\0';
  }

  for (size_t i = 0; i < content_len; ++i) {
    file->content[i] = content[i];
  }
  file->content[content_len] = '\0';
  file->content_len = content_len;
  return LAUNCHER_FS_OK;
}

launcher_fs_result_t launcher_fs_app_read(cap_subject_id_t app_subject_id,
                                          const char *path,
                                          char *out_buffer,
                                          size_t out_buffer_size,
                                          size_t *out_len) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  if (path == 0 || out_buffer == 0 || out_buffer_size == 0) {
    return LAUNCHER_FS_ERR_INVALID_ARG;
  }

  launcher_fs_app_t *app = launcher_fs_find(app_subject_id);
  if (app == 0) {
    return LAUNCHER_FS_ERR_NOT_REGISTERED;
  }

  if (cap_table_check(app_subject_id, CAP_FS_READ) != CAP_OK) {
    return LAUNCHER_FS_ERR_DENIED;
  }

  launcher_fs_file_t *file = launcher_fs_lookup(app, path);
  if (file == 0) {
    return LAUNCHER_FS_ERR_NOT_FOUND;
  }
  if (file->content_len + 1u > out_buffer_size) {
    return LAUNCHER_FS_ERR_NO_SPACE;
  }
  for (size_t i = 0; i < file->content_len; ++i) {
    out_buffer[i] = file->content[i];
  }
  out_buffer[file->content_len] = '\0';
  if (out_len != 0) {
    *out_len = file->content_len;
  }
  return LAUNCHER_FS_OK;
}

launcher_fs_result_t launcher_fs_app_relaunch(cap_subject_id_t app_subject_id) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return LAUNCHER_FS_ERR_INVALID_APP;
  }
  launcher_fs_app_t *app = launcher_fs_find(app_subject_id);
  if (app == 0) {
    return LAUNCHER_FS_ERR_NOT_REGISTERED;
  }

  /* Capability grants are launch-scoped: drop them so the launcher must
   * re-issue any FS access on the next launch. */
  (void)cap_table_revoke(app_subject_id, CAP_FS_READ);
  (void)cap_table_revoke(app_subject_id, CAP_FS_WRITE);

  if (app->mode == LAUNCHER_FS_MODE_EPHEMERAL) {
    launcher_fs_clear_files(app);
  }
  return LAUNCHER_FS_OK;
}

int launcher_fs_app_has_read(cap_subject_id_t app_subject_id) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return 0;
  }
  if (launcher_fs_find(app_subject_id) == 0) {
    return 0;
  }
  return cap_table_check(app_subject_id, CAP_FS_READ) == CAP_OK;
}

int launcher_fs_app_has_write(cap_subject_id_t app_subject_id) {
  if (!launcher_fs_subject_in_range(app_subject_id)) {
    return 0;
  }
  if (launcher_fs_find(app_subject_id) == 0) {
    return 0;
  }
  return cap_table_check(app_subject_id, CAP_FS_WRITE) == CAP_OK;
}

launcher_fs_mode_t launcher_fs_app_mode(cap_subject_id_t app_subject_id) {
  launcher_fs_app_t *app = launcher_fs_find(app_subject_id);
  if (app == 0) {
    return LAUNCHER_FS_MODE_PERSISTENT;
  }
  return app->mode;
}
