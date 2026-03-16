#ifndef SECUREOS_FSLIB_H
#define SECUREOS_FSLIB_H

#include "secureos_api.h"
#include "lib/envlib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int fslib_handle_t;

enum {
  FSLIB_HANDLE_INVALID = 0u,
  FSLIB_PATH_MAX = 128u,
};

typedef enum {
  FSLIB_STATUS_OK = 0,
  FSLIB_STATUS_DENIED = 1,
  FSLIB_STATUS_NOT_FOUND = 2,
  FSLIB_STATUS_ERROR = 3,
} fslib_status_t;

static inline fslib_status_t fslib_from_os_status(os_status_t status) {
  switch (status) {
    case OS_STATUS_OK:
      return FSLIB_STATUS_OK;
    case OS_STATUS_DENIED:
      return FSLIB_STATUS_DENIED;
    case OS_STATUS_NOT_FOUND:
      return FSLIB_STATUS_NOT_FOUND;
    default:
      return FSLIB_STATUS_ERROR;
  }
}

static inline fslib_status_t fslib_chdir(fslib_handle_t handle, const char *path) {
  (void)handle;
  if (path == 0 || path[0] == '\0') {
    return FSLIB_STATUS_ERROR;
  }
  return fslib_from_os_status(os_process_chdir(path));
}

static inline fslib_status_t fslib_getcwd(fslib_handle_t handle,
                                          char *out_buffer,
                                          unsigned int out_buffer_size) {
  (void)handle;
  if (out_buffer == 0 || out_buffer_size == 0u) {
    return FSLIB_STATUS_ERROR;
  }

  if (os_process_getcwd(out_buffer, out_buffer_size) == OS_STATUS_OK && out_buffer[0] != '\0') {
    return FSLIB_STATUS_OK;
  }

  if (envlib_get(ENVLIB_HANDLE_INVALID, "PWD", out_buffer, out_buffer_size) == ENVLIB_STATUS_OK &&
      out_buffer[0] != '\0') {
    return FSLIB_STATUS_OK;
  }

  return FSLIB_STATUS_NOT_FOUND;
}

static inline fslib_status_t fslib_list(fslib_handle_t handle,
                                        const char *path,
                                        char *out_buffer,
                                        unsigned int out_buffer_size) {
  char cwd[FSLIB_PATH_MAX];
  const char *target = path;

  (void)handle;
  if (out_buffer == 0 || out_buffer_size == 0u) {
    return FSLIB_STATUS_ERROR;
  }

  if (target == 0 || target[0] == '\0') {
    if (fslib_getcwd(FSLIB_HANDLE_INVALID, cwd, (unsigned int)sizeof(cwd)) != FSLIB_STATUS_OK) {
      return FSLIB_STATUS_NOT_FOUND;
    }
    target = cwd;
  }

  return fslib_from_os_status(os_fs_list_dir(target, out_buffer, out_buffer_size));
}

static inline fslib_status_t fslib_list_cwd(fslib_handle_t handle,
                                            char *out_buffer,
                                            unsigned int out_buffer_size) {
  return fslib_list(handle, 0, out_buffer, out_buffer_size);
}

static inline fslib_status_t fslib_read(fslib_handle_t handle,
                                        const char *path,
                                        char *out_buffer,
                                        unsigned int out_buffer_size) {
  char resolved[FSLIB_PATH_MAX];
  (void)handle;
  if (path == 0 || path[0] == '\0' || out_buffer == 0 || out_buffer_size == 0u) {
    return FSLIB_STATUS_ERROR;
  }
  envlib_resolve_path(ENVLIB_HANDLE_INVALID, path, resolved, (unsigned int)sizeof(resolved));
  return fslib_from_os_status(os_fs_read_file(resolved, out_buffer, out_buffer_size));
}

static inline fslib_status_t fslib_write(fslib_handle_t handle,
                                         const char *path,
                                         const char *content,
                                         int append) {
  char resolved[FSLIB_PATH_MAX];
  (void)handle;
  if (path == 0 || path[0] == '\0') {
    return FSLIB_STATUS_ERROR;
  }
  envlib_resolve_path(ENVLIB_HANDLE_INVALID, path, resolved, (unsigned int)sizeof(resolved));
  return fslib_from_os_status(os_fs_write_file(resolved, content != 0 ? content : "", append));
}

static inline fslib_status_t fslib_mkdir(fslib_handle_t handle, const char *path) {
  char resolved[FSLIB_PATH_MAX];
  (void)handle;
  if (path == 0 || path[0] == '\0') {
    return FSLIB_STATUS_ERROR;
  }
  envlib_resolve_path(ENVLIB_HANDLE_INVALID, path, resolved, (unsigned int)sizeof(resolved));
  return fslib_from_os_status(os_fs_mkdir(resolved));
}

#ifdef __cplusplus
}
#endif

#endif
