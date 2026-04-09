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

#ifdef __cplusplus
}
#endif

#endif
