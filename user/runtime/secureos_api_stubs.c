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
#include "secureos_abi.h"

enum {
  SECUREOS_NATIVE_BRIDGE_MAGIC = 0x53524247u,
  SECUREOS_NATIVE_BRIDGE_VERSION = 3u,
  SECUREOS_NATIVE_BRIDGE_ADDR = 0x009FF000u,
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
  int (*input_read_char)(char *out_char);
  int (*mouse_get_state)(int *out_x, int *out_y, unsigned char *out_buttons);
  int (*video_clear)(void);
  int (*video_set_cursor)(int col, int row);
  int (*video_putchar_at)(int col, int row, char ch, unsigned char attr);
  int (*video_set_mode)(int mode);
  int (*video_put_pixel)(int x, int y, unsigned char color);
  int (*video_get_pixel)(int x, int y, unsigned char *out_color);
  int (*video_draw_rect)(int x, int y, int w, int h, unsigned char color);
  int (*video_get_resolution)(int *out_width, int *out_height);
  int (*video_blit)(int x, int y, int w, int h, const unsigned char *pixels);
  int (*session_create)(unsigned int *out_session_id);
  int (*session_read_output)(unsigned int session_id, char *out_buffer,
                             unsigned int out_buffer_size, unsigned int *out_len);
  int (*session_write_input)(unsigned int session_id, const char *input,
                             unsigned int len);
  int (*session_tick)(unsigned int session_id);
  int (*auth_poll_prompt)(os_auth_prompt_t *out_prompt);
  int (*auth_respond)(unsigned int slot_index, int response);
  int (*session_read_framebuffer)(unsigned int session_id,
                                  unsigned char *out_pixels,
                                  unsigned int x, unsigned int y,
                                  unsigned int w, unsigned int h);
  int (*session_get_gfx_mode)(unsigned int session_id, int *out_mode);
  int (*session_set_wm_managed)(unsigned int session_id, int managed);
  int (*session_set_vfb_size)(unsigned int session_id,
                              unsigned int width, unsigned int height);
  int (*session_get_vfb_size)(unsigned int session_id,
                              unsigned int *out_width,
                              unsigned int *out_height);
  int (*session_set_virtual_mouse)(unsigned int session_id,
                                   int x, int y, unsigned char buttons);
  int (*mouse_enable)(void);
  int (*mouse_disable)(void);
  /* File I/O */
  int (*fs_read_file)(const char *path, char *out_buffer, unsigned int out_buffer_size);
  int (*fs_write_file)(const char *path, const char *content, int append);
  int (*fs_list_dir)(const char *path, char *out_buffer, unsigned int out_buffer_size);
  int (*fs_mkdir)(const char *path);
  /* Environment */
  int (*env_get)(const char *key, char *out_buffer, unsigned int out_buffer_size);
  int (*env_set)(const char *key, const char *value);
  int (*env_list)(char *out_buffer, unsigned int out_buffer_size);
  /* Process */
  int (*process_getcwd)(char *out_buffer, unsigned int out_buffer_size);
  int (*process_chdir)(const char *path);
  /* M7-TOOLCHAIN-003 (#406): clean process exit.
   * Must be a no-return call when invoked by an in-process app; the
   * kernel side longjmps out of the launcher via the fault-recovery
   * path. Bridge version 2+ only. */
  void (*process_exit)(int status);
  /* M7-TOOLCHAIN-003 slice 2 (#422): synchronous spawn of a staged
   * SOF binary. argv is marshalled by the wrapper into the
   * single space-joined `raw_args` string consumed by the kernel-
   * side `process_run` path; flags is reserved (must be 0).
   * Returns 0 on a successful run (with `*out_exit_status` set to
   * the child's captured exit code), 1 on capability deny, 2 on
   * binary-not-found, 3 on any other failure. Bridge version 3+
   * only. */
  int (*process_spawn)(const char *path,
                       const char *raw_args,
                       unsigned int flags,
                       int *out_exit_status);
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

unsigned int os_get_abi_version(void) {
  /*
   * Version is a pure compile-time constant from secureos_abi.h. No bridge
   * call is required; future syscall-backed runtimes may override this if
   * the runtime needs to disambiguate kernel vs user ABI.
   */
  return (unsigned int)OS_ABI_VERSION;
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
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->fs_list_dir != 0) {
    int rc = bridge->fs_list_dir("/", out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    return OS_STATUS_NOT_FOUND;
  }

  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_fs_list_dir(const char *path, char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->fs_list_dir != 0) {
    int rc = bridge->fs_list_dir(path, out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    return OS_STATUS_NOT_FOUND;
  }

  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }

  if (path == 0 || path[0] == '\0') {
    return OS_STATUS_ERROR;
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_fs_read_file(const char *path, char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->fs_read_file != 0) {
    int rc = bridge->fs_read_file(path, out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    return OS_STATUS_NOT_FOUND;
  }

  (void)path;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_fs_write_file(const char *path, const char *content, int append) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->fs_write_file != 0) {
    int rc = bridge->fs_write_file(path, content, append);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    return OS_STATUS_ERROR;
  }

  (void)path;
  (void)content;
  (void)append;
  return OS_STATUS_OK;
}

os_status_t os_fs_mkdir(const char *path) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->fs_mkdir != 0) {
    int rc = bridge->fs_mkdir(path);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    return OS_STATUS_ERROR;
  }

  (void)path;
  return OS_STATUS_OK;
}

os_status_t os_process_chdir(const char *path) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->process_chdir != 0) {
    int rc = bridge->process_chdir(path);
    if (rc == 0) return OS_STATUS_OK;
    return OS_STATUS_ERROR;
  }

  (void)path;
  return OS_STATUS_OK;
}

os_status_t os_process_exit(int status) {
  /*
   * M7-TOOLCHAIN-003 (#406). When a real bridge is attached the kernel
   * never returns from this call: the bridge thunk runs the launcher
   * teardown and longjmps out via the fault-recovery slot. In
   * host-test / no-bridge builds the call must remain non-fatal so
   * tests can drive the wrapper without terminating the test process,
   * which is why we return `OS_STATUS_OK` rather than `_Exit(status)`.
   */
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->process_exit != 0) {
    bridge->process_exit(status);
    /* Defensive: if the bridge implementation ever returns (it
     * shouldn't), surface that as a generic error rather than letting
     * the caller silently continue. */
    return OS_STATUS_ERROR;
  }

  (void)status;
  return OS_STATUS_OK;
}

/*
 * Internal helper: join `argv[1..]` (NULL-terminated) into the
 * caller-provided buffer, separated by single spaces. argv[0] is
 * the program name, forwarded out-of-band as `path`; the kernel-
 * side launcher reconstructs argv[0] from `path` like every other
 * process_run() call site. Returns 0 on success, -1 if the joined
 * args would overflow `buf_size` (NUL included). On overflow the
 * buffer contents are unspecified.
 */
static int secureos_join_argv(const char *const *argv,
                              char *buf,
                              unsigned int buf_size) {
  unsigned int pos = 0u;
  unsigned int i = 1u;

  if (buf == 0 || buf_size == 0u) {
    return -1;
  }
  buf[0] = '\0';
  if (argv == 0 || argv[0] == 0) {
    return 0;
  }

  for (; argv[i] != 0; ++i) {
    const char *s = argv[i];
    unsigned int j = 0u;
    if (i > 1u) {
      if (pos + 1u >= buf_size) return -1;
      buf[pos++] = ' ';
    }
    while (s[j] != '\0') {
      if (pos + 1u >= buf_size) return -1;
      buf[pos++] = s[j++];
    }
  }
  buf[pos] = '\0';
  return 0;
}

os_status_t os_process_spawn(const char *path,
                              const char *const *argv,
                              unsigned int flags,
                              int *out_exit_status) {
  /*
   * M7-TOOLCHAIN-003 slice 2 (#422). Synchronous spawn of a staged
   * SOF binary through the existing launcher / capability gate.
   * Argv is space-joined into the kernel `raw_args` contract; env
   * marshalling is intentionally out of scope for this slice.
   *
   * Status mapping (kernel bridge int -> os_status_t):
   *   0 -> OS_STATUS_OK       (clean run; *out_exit_status valid)
   *   1 -> OS_STATUS_DENIED   (missing CAP_APP_EXEC, audited)
   *   2 -> OS_STATUS_NOT_FOUND (binary not present)
   *   else -> OS_STATUS_ERROR (bad args, format, spawn-time fail)
   */
  secureos_native_bridge_t *bridge;
  char joined_args[256];

  /* Validate inputs BEFORE touching the bridge pointer so the
   * early-reject paths (NULL/empty path, reserved flag bits) are
   * safe to exercise on the host without a mapped bridge. The
   * host link smoke test in `tests/process_spawn_wrapper_test.c`
   * depends on this ordering. */
  if (path == 0 || path[0] == '\0') {
    return OS_STATUS_ERROR;
  }
  if (flags != 0u) {
    /* Reserved flag bits — refuse rather than silently ignore so the
     * meaning of a future flag is never grandfathered into the v0
     * surface. */
    return OS_STATUS_ERROR;
  }

  if (secureos_join_argv(argv, joined_args, (unsigned int)sizeof(joined_args)) != 0) {
    return OS_STATUS_ERROR;
  }

  bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->process_spawn != 0) {
    int rc = bridge->process_spawn(path, joined_args, flags, out_exit_status);
    if (rc == 0) return OS_STATUS_OK;
    if (rc == 1) return OS_STATUS_DENIED;
    if (rc == 2) return OS_STATUS_NOT_FOUND;
    return OS_STATUS_ERROR;
  }

  /* No-bridge / host-test path: succeed silently so the wrapper is
   * link-testable without a live kernel. Mirrors the
   * `os_process_exit` host-build contract. */
  (void)out_exit_status;
  return OS_STATUS_OK;
}

os_status_t os_process_getcwd(char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->process_getcwd != 0) {
    int rc = bridge->process_getcwd(out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    return OS_STATUS_ERROR;
  }

  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '/';
    if (out_buffer_size > 1u) {
      out_buffer[1] = '\0';
    }
  }
  return OS_STATUS_OK;
}

os_status_t os_env_get(const char *key, char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->env_get != 0) {
    int rc = bridge->env_get(key, out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    return OS_STATUS_NOT_FOUND;
  }

  (void)key;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_env_set(const char *key, const char *value) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->env_set != 0) {
    int rc = bridge->env_set(key, value);
    if (rc == 0) return OS_STATUS_OK;
    return OS_STATUS_ERROR;
  }

  (void)key;
  (void)value;
  return OS_STATUS_OK;
}

os_status_t os_env_list(char *out_buffer, unsigned int out_buffer_size) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->env_list != 0) {
    int rc = bridge->env_list(out_buffer, out_buffer_size);
    if (rc == 0) return OS_STATUS_OK;
    return OS_STATUS_ERROR;
  }

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

/* envlib stub implementations - required for apps that use fslib which includes envlib.h */
#include "lib/envlib.h"

envlib_status_t envlib_get(envlib_handle_t handle, const char *key, char *out_value,
                           unsigned int out_value_size) {
  (void)handle;
  (void)key;
  if (out_value != 0 && out_value_size > 0u) {
    out_value[0] = '\0';
  }
  return ENVLIB_STATUS_NOT_FOUND;
}

envlib_status_t envlib_set(envlib_handle_t handle, const char *key, const char *value) {
  (void)handle;
  (void)key;
  (void)value;
  return ENVLIB_STATUS_OK;
}

envlib_status_t envlib_list(envlib_handle_t handle, char *out_buffer, unsigned int out_buffer_size) {
  (void)handle;
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return ENVLIB_STATUS_OK;
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

os_status_t os_clock_get(char *out_buffer, unsigned int out_buffer_size) {
  if (out_buffer != 0 && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return OS_STATUS_OK;
}

os_status_t os_clock_set(const char *datetime_str) {
  (void)datetime_str;
  return OS_STATUS_DENIED;
}

os_status_t os_clock_epoch(unsigned int *out) {
  if (out != 0) {
    *out = 0u;
  }
  return OS_STATUS_OK;
}

os_status_t os_clock_ticks(unsigned int *out) {
  if (out != 0) {
    *out = 0u;
  }
  return OS_STATUS_OK;
}

os_status_t os_input_read_char(char *out_char) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (out_char == 0) {
    return OS_STATUS_ERROR;
  }

  if (bridge != 0 && bridge->input_read_char != 0) {
    return (os_status_t)bridge->input_read_char(out_char);
  }

  *out_char = '\0';
  return OS_STATUS_ERROR;
}

os_status_t os_mouse_get_state(int *out_x, int *out_y, unsigned char *out_buttons) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->mouse_get_state != 0) {
    return (os_status_t)bridge->mouse_get_state(out_x, out_y, out_buttons);
  }

  if (out_x != 0) *out_x = 0;
  if (out_y != 0) *out_y = 0;
  if (out_buttons != 0) *out_buttons = 0;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_mouse_enable(void) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->mouse_enable != 0) {
    return (os_status_t)bridge->mouse_enable();
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_mouse_disable(void) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->mouse_disable != 0) {
    return (os_status_t)bridge->mouse_disable();
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_clear(void) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_clear != 0) {
    return (os_status_t)bridge->video_clear();
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_set_cursor(int col, int row) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_set_cursor != 0) {
    return (os_status_t)bridge->video_set_cursor(col, row);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_putchar_at(int col, int row, char ch, unsigned char attr) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_putchar_at != 0) {
    return (os_status_t)bridge->video_putchar_at(col, row, ch, attr);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_set_mode(int mode) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_set_mode != 0) {
    return (os_status_t)bridge->video_set_mode(mode);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_put_pixel(int x, int y, unsigned char color) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_put_pixel != 0) {
    return (os_status_t)bridge->video_put_pixel(x, y, color);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_get_pixel(int x, int y, unsigned char *out_color) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_get_pixel != 0) {
    return (os_status_t)bridge->video_get_pixel(x, y, out_color);
  }

  if (out_color != 0) *out_color = 0;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_draw_rect(int x, int y, int w, int h, unsigned char color) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_draw_rect != 0) {
    return (os_status_t)bridge->video_draw_rect(x, y, w, h, color);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_get_resolution(int *out_width, int *out_height) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_get_resolution != 0) {
    return (os_status_t)bridge->video_get_resolution(out_width, out_height);
  }

  if (out_width != 0) *out_width = 0;
  if (out_height != 0) *out_height = 0;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_video_blit(int x, int y, int w, int h, const unsigned char *pixels) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->video_blit != 0) {
    return (os_status_t)bridge->video_blit(x, y, w, h, pixels);
  }

  /* Fallback: draw pixel-by-pixel via put_pixel */
  if (bridge != 0 && bridge->video_put_pixel != 0 && pixels != 0) {
    int row, col;
    for (row = 0; row < h; row++) {
      for (col = 0; col < w; col++) {
        bridge->video_put_pixel(x + col, y + row, pixels[row * w + col]);
      }
    }
    return OS_STATUS_OK;
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_create(unsigned int *out_session_id) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_create != 0) {
    return (os_status_t)bridge->session_create(out_session_id);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_read_output(unsigned int session_id, char *out_buffer,
                                   unsigned int out_buffer_size,
                                   unsigned int *out_len) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_read_output != 0) {
    return (os_status_t)bridge->session_read_output(session_id, out_buffer,
                                                    out_buffer_size, out_len);
  }

  if (out_len != 0) *out_len = 0;
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_write_input(unsigned int session_id, const char *input,
                                   unsigned int len) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_write_input != 0) {
    return (os_status_t)bridge->session_write_input(session_id, input, len);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_tick(unsigned int session_id) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_tick != 0) {
    return (os_status_t)bridge->session_tick(session_id);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_set_wm_managed(unsigned int session_id, int managed) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_set_wm_managed != 0) {
    return (os_status_t)bridge->session_set_wm_managed(session_id, managed);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_set_vfb_size(unsigned int session_id,
                                    unsigned int width, unsigned int height) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_set_vfb_size != 0) {
    return (os_status_t)bridge->session_set_vfb_size(session_id, width, height);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_get_vfb_size(unsigned int session_id,
                                    unsigned int *out_width,
                                    unsigned int *out_height) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_get_vfb_size != 0) {
    return (os_status_t)bridge->session_get_vfb_size(session_id,
                                                     out_width, out_height);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_auth_poll_prompt(os_auth_prompt_t *out_prompt) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->auth_poll_prompt != 0) {
    return (os_status_t)bridge->auth_poll_prompt(out_prompt);
  }

  if (out_prompt != 0) {
    out_prompt->active = 0;
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_auth_respond(unsigned int slot_index, int response) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->auth_respond != 0) {
    return (os_status_t)bridge->auth_respond(slot_index, response);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_read_framebuffer(unsigned int session_id,
                                        unsigned char *out_pixels,
                                        unsigned int x, unsigned int y,
                                        unsigned int w, unsigned int h) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_read_framebuffer != 0) {
    return (os_status_t)bridge->session_read_framebuffer(
        session_id, out_pixels, x, y, w, h);
  }

  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_get_gfx_mode(unsigned int session_id, int *out_mode) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_get_gfx_mode != 0) {
    return (os_status_t)bridge->session_get_gfx_mode(session_id, out_mode);
  }

  if (out_mode != 0) {
    *out_mode = 0;
  }
  return OS_STATUS_NOT_FOUND;
}

os_status_t os_session_set_virtual_mouse(unsigned int session_id,
                                         int x, int y,
                                         unsigned char buttons) {
  secureos_native_bridge_t *bridge = secureos_native_bridge();

  if (bridge != 0 && bridge->session_set_virtual_mouse != 0) {
    return (os_status_t)bridge->session_set_virtual_mouse(session_id, x, y,
                                                          buttons);
  }

  return OS_STATUS_NOT_FOUND;
}
