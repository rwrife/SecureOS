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
