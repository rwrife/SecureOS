#ifndef SECUREOS_APP_RUNTIME_H
#define SECUREOS_APP_RUNTIME_H

#include <stddef.h>

#include "../cap/capability.h"

typedef void (*app_runtime_output_fn)(const char *message);
typedef cap_access_state_t (*app_runtime_authorize_fn)(const char *operation, const char *path);

typedef struct {
  cap_subject_id_t subject_id;
  app_runtime_output_fn output;
  app_runtime_authorize_fn authorize_disk_io;
} app_runtime_context_t;

typedef enum {
  APP_RUNTIME_OK = 0,
  APP_RUNTIME_ERR_INVALID_ARG = 1,
  APP_RUNTIME_ERR_NOT_FOUND = 2,
  APP_RUNTIME_ERR_CAPABILITY = 3,
  APP_RUNTIME_ERR_DENIED = 4,
  APP_RUNTIME_ERR_STORAGE = 5,
} app_runtime_result_t;

size_t app_runtime_list(char *out_buffer, size_t out_buffer_size);
app_runtime_result_t app_runtime_run(const char *app_name, const app_runtime_context_t *context);

#endif
