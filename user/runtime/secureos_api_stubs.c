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
 *   - All user apps (filedemo and os commands): link against these
 *     stubs at build time.
 *
 * Launched by:
 *   Not a standalone process.  Compiled into the user-space runtime
 *   library that is linked with every user application.
 */

#include "secureos_api.h"

enum {
  SECUREOS_NATIVE_BRIDGE_MAGIC = 0x53524247u,
  SECUREOS_NATIVE_BRIDGE_VERSION = 1u,
  SECUREOS_NATIVE_BRIDGE_ADDR = 0x003FF000u,
};

typedef struct {
  unsigned int magic;
  unsigned int version;
  unsigned int reserved0;
  unsigned int reserved1;
  int (*console_write)(const char *message);
  int (*get_args)(char *out_buffer, unsigned int out_buffer_size);
  int (*net_device_ready)(void);
  int (*net_device_backend)(char *out_buffer, unsigned int out_buffer_size);
  int (*net_device_get_mac)(unsigned char *out_buffer, unsigned int out_buffer_size);
  int (*net_frame_send)(const unsigned char *frame, unsigned int frame_len);
  int (*net_frame_recv)(unsigned char *out_buffer,
                        unsigned int out_buffer_size,
                        unsigned int *out_frame_len);
  const char *raw_args;
} secureos_native_bridge_t;

static secureos_native_bridge_t *secureos_native_bridge(void) {
  secureos_native_bridge_t *bridge = (secureos_native_bridge_t *)(unsigned long)SECUREOS_NATIVE_BRIDGE_ADDR;

  if (bridge == 0) {
    return 0;
  }
  if (bridge->magic != SECUREOS_NATIVE_BRIDGE_MAGIC ||
      bridge->version != SECUREOS_NATIVE_BRIDGE_VERSION) {
    return 0;
  }
  return bridge;
}

os_status_t os_console_write(const char *message) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->console_write != 0) {
    return (os_status_t)bridge->console_write(message);
  }

  (void)message;
  return OS_STATUS_OK;
}

os_status_t os_fs_list_root(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_fs_list_dir(const char *path, char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }

  if (path == 0 || path[0] == '\0') {
    return OS_STATUS_ERROR;
  }

  if (path[0] == '/' && path[1] == '\0') {
    return os_fs_list_root(out_buffer, out_buffer_size);
  }

  return OS_STATUS_NOT_FOUND;
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

os_status_t os_process_getcwd(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '/';
    if (out_buffer_size > 1u) {
      out_buffer[1] = '\0';
    }
  }
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

os_status_t os_net_device_ready(void) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->net_device_ready != 0) {
    return (os_status_t)bridge->net_device_ready();
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_device_backend(char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->net_device_backend != 0) {
    return (os_status_t)bridge->net_device_backend(out_buffer, out_buffer_size);
  }

  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_device_get_mac(unsigned char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();
  unsigned int i = 0u;

  if (bridge != 0 && bridge->net_device_get_mac != 0) {
    return (os_status_t)bridge->net_device_get_mac(out_buffer, out_buffer_size);
  }

  if (out_buffer != 0) {
    for (i = 0u; i < out_buffer_size; ++i) {
      out_buffer[i] = 0u;
    }
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_frame_send(const unsigned char *frame, unsigned int frame_len) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->net_frame_send != 0) {
    return (os_status_t)bridge->net_frame_send(frame, frame_len);
  }

  (void)frame;
  (void)frame_len;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_frame_recv(unsigned char *out_buffer,
                              unsigned int out_buffer_size,
                              unsigned int *out_frame_len) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->net_frame_recv != 0) {
    return (os_status_t)bridge->net_frame_recv(out_buffer, out_buffer_size, out_frame_len);
  }

  (void)out_buffer;
  (void)out_buffer_size;
  if (out_frame_len != 0) {
    *out_frame_len = 0u;
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_ifconfig(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_http_get(const char *url, char *out_buffer, unsigned int out_buffer_size) {
  (void)url;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_https_get(const char *url, char *out_buffer, unsigned int out_buffer_size) {
  (void)url;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_net_ping(const char *host, char *out_buffer, unsigned int out_buffer_size) {
  (void)host;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
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
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->get_args != 0) {
    return (os_status_t)bridge->get_args(out_buffer, out_buffer_size);
  }

  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}
