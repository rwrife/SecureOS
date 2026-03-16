#ifndef SECUREOS_ENVLIB_H
#define SECUREOS_ENVLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int envlib_handle_t;

enum {
  ENVLIB_HANDLE_INVALID = 0u,
};

typedef enum {
  ENVLIB_STATUS_OK = 0,
  ENVLIB_STATUS_DENIED = 1,
  ENVLIB_STATUS_NOT_FOUND = 2,
  ENVLIB_STATUS_ERROR = 3,
} envlib_status_t;

envlib_status_t envlib_get(envlib_handle_t handle,
                           const char *key,
                           char *out_value,
                           unsigned int out_value_size);
envlib_status_t envlib_set(envlib_handle_t handle, const char *key, const char *value);
envlib_status_t envlib_list(envlib_handle_t handle, char *out_buffer, unsigned int out_buffer_size);

/*
 * Resolve a path against the PWD environment variable.
 *
 * Absolute paths (starting with '/' or '\') are copied unchanged.
 * Relative paths are prefixed with the current PWD value; if PWD is
 * not set or the lookup fails the path is resolved against '/'.
 */
static inline void envlib_resolve_path(envlib_handle_t handle,
                                       const char *path,
                                       char *out,
                                       unsigned int out_size) {
  char cwd[64];
  unsigned int i = 0u;
  unsigned int j = 0u;

  if (out == 0 || out_size == 0u) {
    return;
  }

  if (path == 0 || path[0] == '\0') {
    out[0] = '\0';
    return;
  }

  /* Absolute path — pass through unchanged */
  if (path[0] == '/' || path[0] == '\\') {
    while (path[j] != '\0' && j + 1u < out_size) {
      out[j] = path[j];
      j++;
    }
    out[j] = '\0';
    return;
  }

  /* Relative path — prepend PWD */
  if (envlib_get(handle, "PWD", cwd, sizeof(cwd)) != ENVLIB_STATUS_OK) {
    cwd[0] = '/';
    cwd[1] = '\0';
  }

  while (cwd[i] != '\0' && i + 1u < out_size) {
    out[i] = cwd[i];
    i++;
  }
  if (i > 0u && out[i - 1u] != '/' && i + 1u < out_size) {
    out[i++] = '/';
  }
  while (path[j] != '\0' && i + 1u < out_size) {
    out[i++] = path[j++];
  }
  out[i] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif
