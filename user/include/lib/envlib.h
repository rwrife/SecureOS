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

#ifdef __cplusplus
}
#endif

#endif
