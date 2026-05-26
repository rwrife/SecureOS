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

/* Input syscalls */
os_status_t os_input_read_char(char *out_char);
os_status_t os_mouse_get_state(int *out_x, int *out_y, unsigned char *out_buttons);

/**
 * Enable system-managed mouse cursor rendering.
 * When enabled, the kernel (or WM) renders the cursor automatically.
 * Apps just call os_mouse_get_state() for position/buttons — no need
 * to draw their own cursor. Call before entering the event loop.
 */
os_status_t os_mouse_enable(void);

/**
 * Disable system-managed mouse cursor rendering.
 * Call when the app no longer needs mouse (e.g., before exiting gfx mode).
 */
os_status_t os_mouse_disable(void);

/* Video syscalls for direct screen manipulation */
os_status_t os_video_clear(void);
os_status_t os_video_set_cursor(int col, int row);
os_status_t os_video_putchar_at(int col, int row, char ch, unsigned char attr);

/* Video mode constants */
#define OS_VIDEO_MODE_TEXT    0  /* 80x25 text mode (default) */
#define OS_VIDEO_MODE_GFX    1  /* 320x200x256 graphics mode */

#define OS_GFX_WIDTH   320
#define OS_GFX_HEIGHT  200

/* Graphics-mode pixel drawing */
os_status_t os_video_set_mode(int mode);
os_status_t os_video_put_pixel(int x, int y, unsigned char color);
os_status_t os_video_get_pixel(int x, int y, unsigned char *out_color);
os_status_t os_video_draw_rect(int x, int y, int w, int h, unsigned char color);
os_status_t os_video_get_resolution(int *out_width, int *out_height);

/**
 * Bulk-copy a pixel buffer to the VGA framebuffer starting at (x, y).
 * The buffer contains w*h bytes in row-major order (one byte per pixel,
 * palette index). Pixels outside screen bounds are clipped.
 */
os_status_t os_video_blit(int x, int y, int w, int h, const unsigned char *pixels);

/* Session management syscalls (used by window manager) */
os_status_t os_session_create(unsigned int *out_session_id);
os_status_t os_session_read_output(unsigned int session_id, char *out_buffer,
                                   unsigned int out_buffer_size,
                                   unsigned int *out_len);
os_status_t os_session_write_input(unsigned int session_id, const char *input,
                                   unsigned int len);
os_status_t os_session_tick(unsigned int session_id);
os_status_t os_session_set_wm_managed(unsigned int session_id, int managed);

/* Auth prompt polling and response (for window manager) */
#define AUTH_TYPE_DISK_IO       0
#define AUTH_TYPE_UNSIGNED_BIN  1

#define AUTH_RESP_DENY          0
#define AUTH_RESP_ALLOW         1
#define AUTH_RESP_ALLOW_ALWAYS  2

typedef struct {
    int active;
    unsigned int session_id;
    int type;
    char description[128];
    unsigned int slot_index;
} os_auth_prompt_t;

os_status_t os_auth_poll_prompt(os_auth_prompt_t *out_prompt);
os_status_t os_auth_respond(unsigned int slot_index, int response);

/* Virtual framebuffer access (for window manager) */
os_status_t os_session_read_framebuffer(unsigned int session_id,
                                        unsigned char *out_pixels,
                                        unsigned int x, unsigned int y,
                                        unsigned int w, unsigned int h);
os_status_t os_session_get_gfx_mode(unsigned int session_id, int *out_mode);

/* Virtual mouse injection (for window manager to provide mouse to windowed apps) */
os_status_t os_session_set_virtual_mouse(unsigned int session_id,
                                         int x, int y,
                                         unsigned char buttons);

#ifdef __cplusplus
}
#endif

#endif
