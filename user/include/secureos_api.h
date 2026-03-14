#ifndef SECUREOS_API_H
#define SECUREOS_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OS_STATUS_OK = 0,
  OS_STATUS_DENIED = 1,
  OS_STATUS_NOT_FOUND = 2,
  OS_STATUS_ERROR = 3,
} os_status_t;

/*
 * These are ABI placeholders for early user-app compilation.
 * Kernel syscall wiring will provide concrete implementations.
 */
os_status_t os_console_write(const char *message);
os_status_t os_fs_list_root(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_fs_read_file(const char *path, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_fs_write_file(const char *path, const char *content, int append);

#ifdef __cplusplus
}
#endif

#endif
