/**
 * @file secureos_api_stubs.c
 * @brief Stub implementations of SecureOS system-call wrappers.
 *
 * Purpose:
 *   Provides no-op stub implementations for every function declared in
 *   secureos_api.h.  In a full system these would trap into the kernel;
 *   for now they allow user-space applications to link and run inside
 *   the build-time test harness without a real syscall layer.
 *
 * Interactions:
 *   - secureos_api.h: implements the complete user-space API surface
 *     (console, filesystem, environment, library, storage, args).
 *   - All user apps (filedemo, os/* commands): link against these
 *     stubs at build time.
 *
 * Launched by:
 *   Not a standalone process.  Compiled into the user-space runtime
 *   library that is linked with every user application.
 */

#include "secureos_api.h"

os_status_t os_console_write(const char *message) {
  (void)message;
  return OS_STATUS_OK;
}

os_status_t os_fs_list_root(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_fs_read_file(const char *path, char *out_buffer, unsigned int out_buffer_size) {
  (void)path;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_fs_write_file(const char *path, const char *content, int append) {
  (void)path;
  (void)content;
  (void)append;
  return OS_STATUS_OK;
}

os_status_t os_fs_mkdir(const char *path) {
  (void)path;
  return OS_STATUS_OK;
}

os_status_t os_process_chdir(const char *path) {
  (void)path;
  return OS_STATUS_OK;
}

os_status_t os_env_get(const char *key, char *out_buffer, unsigned int out_buffer_size) {
  (void)key;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_env_set(const char *key, const char *value) {
  (void)key;
  (void)value;
  return OS_STATUS_OK;
}

os_status_t os_env_list(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_lib_list(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_lib_load(const char *path, char *out_buffer, unsigned int out_buffer_size) {
  (void)path;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_lib_unload(unsigned int handle) {
  (void)handle;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_apps_list(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_storage_info(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_get_args(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}
