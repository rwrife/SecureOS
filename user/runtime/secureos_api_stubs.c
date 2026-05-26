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
  SECUREOS_NATIVE_BRIDGE_VERSION = 1u,
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
