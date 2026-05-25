#ifndef SECUREOS_API_H
#define SECUREOS_API_H

#include "secureos_abi.h"

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
os_status_t os_fs_list_dir(const char *path, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_fs_read_file(const char *path, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_fs_write_file(const char *path, const char *content, int append);
os_status_t os_fs_mkdir(const char *path);
os_status_t os_process_chdir(const char *path);
os_status_t os_process_getcwd(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_env_get(const char *key, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_env_set(const char *key, const char *value);
os_status_t os_env_list(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_lib_list(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_lib_load(const char *path, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_lib_unload(unsigned int handle);
os_status_t os_net_device_ready(void);
os_status_t os_net_device_backend(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_net_device_get_mac(unsigned char *out_buffer, unsigned int out_buffer_size);
os_status_t os_net_frame_send(const unsigned char *frame, unsigned int frame_len);
os_status_t os_net_frame_recv(unsigned char *out_buffer,
                              unsigned int out_buffer_size,
                              unsigned int *out_frame_len);
os_status_t os_net_ifconfig(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_net_http_get(const char *url, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_net_https_get(const char *url, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_net_ping(const char *host, char *out_buffer, unsigned int out_buffer_size);
os_status_t os_apps_list(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_storage_info(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_get_args(char *out_buffer, unsigned int out_buffer_size);

/* Clock / time syscalls */
os_status_t os_clock_get(char *out_buffer, unsigned int out_buffer_size);
os_status_t os_clock_set(const char *datetime_str);
os_status_t os_clock_epoch(unsigned int *out);
os_status_t os_clock_ticks(unsigned int *out);

/*
 * Information-only accessor for the runtime ABI version. Returns the same
 * packed (major << 16) | minor value defined by OS_ABI_VERSION in
 * secureos_abi.h. Intentionally gated by no capability: version queries
 * carry no authority and must be safe to call from any user app.
 */
unsigned int os_get_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
