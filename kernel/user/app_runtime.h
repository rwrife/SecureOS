#ifndef SECUREOS_APP_RUNTIME_H
#define SECUREOS_APP_RUNTIME_H

#include <stddef.h>

#include "../cap/capability.h"

typedef void (*app_runtime_output_fn)(const char *message);
typedef cap_access_state_t (*app_runtime_authorize_fn)(const char *operation, const char *path);
typedef void (*app_runtime_path_resolve_fn)(const char *input_path, char *out_path, size_t out_path_size);
typedef int (*app_runtime_change_dir_fn)(const char *absolute_path);
typedef int (*app_runtime_get_env_fn)(const char *key, char *out_value, size_t out_value_size);
typedef int (*app_runtime_set_env_fn)(const char *key, const char *value);
typedef size_t (*app_runtime_list_env_fn)(char *out_buffer, size_t out_buffer_size);
typedef int (*app_runtime_register_library_fn)(const char *resolved_path,
                                               size_t program_len,
                                               const char *owner_actor,
                                               unsigned int *out_handle);
typedef int (*app_runtime_unregister_library_fn)(unsigned int handle,
                                                 char *out_path,
                                                 size_t out_path_size);
typedef int (*app_runtime_get_loaded_library_ref_count_fn)(unsigned int handle,
                                                            unsigned int *out_ref_count);
typedef int (*app_runtime_acquire_loaded_library_fn)(unsigned int handle,
                                                     unsigned int *out_ref_count);
typedef int (*app_runtime_release_loaded_library_fn)(unsigned int handle,
                                                     unsigned int *out_ref_count);
typedef size_t (*app_runtime_list_loaded_libraries_fn)(char *out_buffer, size_t out_buffer_size);

typedef struct {
  cap_subject_id_t subject_id;
  const char *actor_name;
  app_runtime_output_fn output;
  app_runtime_authorize_fn authorize_disk_io;
  app_runtime_path_resolve_fn resolve_path;
  app_runtime_change_dir_fn change_directory;
  app_runtime_get_env_fn get_env;
  app_runtime_set_env_fn set_env;
  app_runtime_list_env_fn list_env;
  app_runtime_register_library_fn register_loaded_library;
  app_runtime_unregister_library_fn unregister_loaded_library;
  app_runtime_get_loaded_library_ref_count_fn get_loaded_library_ref_count;
  app_runtime_acquire_loaded_library_fn acquire_loaded_library;
  app_runtime_release_loaded_library_fn release_loaded_library;
  app_runtime_list_loaded_libraries_fn list_loaded_libraries;
} app_runtime_context_t;

typedef enum {
  APP_RUNTIME_OK = 0,
  APP_RUNTIME_ERR_INVALID_ARG = 1,
  APP_RUNTIME_ERR_NOT_FOUND = 2,
  APP_RUNTIME_ERR_CAPABILITY = 3,
  APP_RUNTIME_ERR_DENIED = 4,
  APP_RUNTIME_ERR_STORAGE = 5,
  APP_RUNTIME_ERR_FORMAT = 6,
  APP_RUNTIME_ERR_LIBRARY = 7,
  APP_RUNTIME_ERR_IN_USE = 8,
} app_runtime_result_t;

enum {
  APP_RUNTIME_LIBRARY_PATH_MAX = 64,
};

typedef struct {
  char resolved_path[APP_RUNTIME_LIBRARY_PATH_MAX];
  size_t program_len;
} app_runtime_library_info_t;

size_t app_runtime_list(char *out_buffer, size_t out_buffer_size);
size_t app_runtime_list_libraries(char *out_buffer, size_t out_buffer_size);
app_runtime_result_t app_runtime_load_library(const char *library_name,
                                              const app_runtime_context_t *context,
                                              app_runtime_library_info_t *out_library);
app_runtime_result_t app_runtime_run(const char *app_name,
                                     const char *app_args,
                                     const app_runtime_context_t *context);

#endif
