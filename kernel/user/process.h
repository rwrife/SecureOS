/**
 * @file process.h
 * @brief Process launch API for user program execution and management.
 *
 * Purpose:
 *   Provides a unified interface for launching user-space processes, loading
 *   libraries, and managing application execution. Loads ELF binaries from the
 *   filesystem, validates their format, and executes them. Supports both native
 *   user-space entry points and a built-in script interpreter for lightweight
 *   shell commands. Manages per-invocation argument passing, environment variable
 *   expansion, and capability-gated disk I/O authorization.
 *
 * Interactions:
 *   - fs_service.c: reads application ELF files from the filesystem
 *     via fs_read_file_bytes.
 *   - cap_table.c: checks capabilities before allowing execution and storage
 *     operations.
 *   - console.c: the console dispatches user commands through process_run,
 *     providing output and authorization callbacks.
 *   - storage_hal.c: storage info queries are forwarded to the HAL.
 *
 * Launched by:
 *   process_run() is called by console.c when the user invokes a command.
 *   Not a standalone process; compiled into the kernel image.
 */

#ifndef SECUREOS_PROCESS_H
#define SECUREOS_PROCESS_H

#include <stddef.h>

#include "../cap/capability.h"

typedef void (*process_output_fn)(const char *message);
typedef cap_access_state_t (*process_authorize_fn)(const char *operation, const char *path);
typedef void (*process_path_resolve_fn)(const char *input_path, char *out_path, size_t out_path_size);
typedef int (*process_change_dir_fn)(const char *absolute_path);
typedef int (*process_get_env_fn)(const char *key, char *out_value, size_t out_value_size);
typedef int (*process_set_env_fn)(const char *key, const char *value);
typedef size_t (*process_list_env_fn)(char *out_buffer, size_t out_buffer_size);
typedef int (*process_register_library_fn)(const char *resolved_path,
                                            size_t program_len,
                                            const char *owner_actor,
                                            unsigned int *out_handle);
typedef int (*process_unregister_library_fn)(unsigned int handle,
                                              char *out_path,
                                              size_t out_path_size);
typedef int (*process_get_loaded_library_ref_count_fn)(unsigned int handle,
                                                        unsigned int *out_ref_count);
typedef int (*process_acquire_loaded_library_fn)(unsigned int handle,
                                                  unsigned int *out_ref_count);
typedef int (*process_release_loaded_library_fn)(unsigned int handle,
                                                  unsigned int *out_ref_count);
typedef size_t (*process_list_loaded_libraries_fn)(char *out_buffer, size_t out_buffer_size);

typedef struct {
  cap_subject_id_t subject_id;
  const char *actor_name;
  process_output_fn output;
  process_authorize_fn authorize_disk_io;
  process_path_resolve_fn resolve_path;
  process_change_dir_fn change_directory;
  process_get_env_fn get_env;
  process_set_env_fn set_env;
  process_list_env_fn list_env;
  process_register_library_fn register_loaded_library;
  process_unregister_library_fn unregister_loaded_library;
  process_get_loaded_library_ref_count_fn get_loaded_library_ref_count;
  process_acquire_loaded_library_fn acquire_loaded_library;
  process_release_loaded_library_fn release_loaded_library;
  process_list_loaded_libraries_fn list_loaded_libraries;
} process_context_t;

typedef enum {
  PROCESS_OK = 0,
  PROCESS_ERR_INVALID_ARG = 1,
  PROCESS_ERR_NOT_FOUND = 2,
  PROCESS_ERR_CAPABILITY = 3,
  PROCESS_ERR_DENIED = 4,
  PROCESS_ERR_STORAGE = 5,
  PROCESS_ERR_FORMAT = 6,
  PROCESS_ERR_LIBRARY = 7,
  PROCESS_ERR_IN_USE = 8,
} process_result_t;

enum {
  PROCESS_LIBRARY_PATH_MAX = 64,
};

typedef struct {
  char resolved_path[PROCESS_LIBRARY_PATH_MAX];
  size_t program_len;
} process_library_info_t;

size_t process_list_apps(char *out_buffer, size_t out_buffer_size);
size_t process_list_libraries(char *out_buffer, size_t out_buffer_size);
process_result_t process_load_library(const char *library_name,
                                      const process_context_t *context,
                                      process_library_info_t *out_library);
process_result_t process_run(const char *program_name,
                             const char *program_args,
                             const process_context_t *context);

#endif
