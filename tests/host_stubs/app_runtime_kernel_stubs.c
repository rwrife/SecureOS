/**
 * @file app_runtime_kernel_stubs.c
 * @brief Host-side stub implementations for kernel symbols referenced by
 *        launcher_exec.c that are not exercised by app_runtime_test.c.
 *
 * Purpose:
 *   `tests/app_runtime_test.c` host-links `kernel/user/launcher_exec.c` to
 *   validate the script interpreter, capability/cwd plumbing, and ELF
 *   execution paths. Over time launcher_exec.c grew dependencies on a
 *   number of kernel subsystems (session manager, event bus, serial HAL,
 *   IDT fault recovery, VGA gfx, mouse HAL, clock service) whose real
 *   implementations either pull in freestanding-only code, depend on
 *   architecture-specific assembly (`fault_recover_*`), or assume kernel
 *   linkage. The app_runtime test does not drive any of those code paths
 *   end-to-end, so we provide no-op stubs here just to satisfy the host
 *   linker.
 *
 *   This file is intentionally the only thing keeping these symbols
 *   reachable on the host: if the test ever needs to drive real behaviour
 *   through one of these subsystems, replace the relevant stub with a
 *   proper compile-in of the source file.
 *
 * Referenced from:
 *   build/scripts/test_app_runtime.sh
 *
 * Filed under issue #341 (app_runtime regression) — host-link only,
 * never compiled into the kernel image.
 */

#include <stddef.h>
#include <stdint.h>

#include "../../kernel/event/event_bus.h"
#include "../../kernel/hal/clock_hal.h"
#include "../../kernel/hal/mouse_hal.h"

/* ------------------------------------------------------------------ */
/* session_manager.c                                                  */
/* ------------------------------------------------------------------ */

int session_manager_create(unsigned int subject_id, unsigned int *out_session_id) {
  (void)subject_id;
  if (out_session_id) {
    *out_session_id = 0u;
  }
  return -1;
}

void session_manager_destroy(unsigned int session_id) { (void)session_id; }

unsigned int session_manager_active_id(void) { return 0u; }

size_t session_manager_read_output(unsigned int session_id, char *out_buffer,
                                   size_t out_buffer_size) {
  (void)session_id;
  if (out_buffer && out_buffer_size > 0u) {
    out_buffer[0] = '\0';
  }
  return 0u;
}

size_t session_manager_write_input(unsigned int session_id, const char *input,
                                   size_t len) {
  (void)session_id;
  (void)input;
  (void)len;
  return 0u;
}

void session_manager_tick(unsigned int session_id) { (void)session_id; }

void session_manager_set_wm_managed(unsigned int session_id, int managed) {
  (void)session_id;
  (void)managed;
}

int session_manager_is_wm_managed(unsigned int session_id) {
  (void)session_id;
  return 0;
}

int session_manager_get_gfx_mode(unsigned int session_id) {
  (void)session_id;
  return 0;
}

void session_manager_set_gfx_mode(unsigned int session_id, int gfx_mode) {
  (void)session_id;
  (void)gfx_mode;
}

void session_manager_set_virtual_mouse(unsigned int session_id,
                                       int x, int y,
                                       unsigned char buttons) {
  (void)session_id;
  (void)x;
  (void)y;
  (void)buttons;
}

void session_manager_set_vfb_size(unsigned int session_id,
                                  unsigned int width, unsigned int height) {
  (void)session_id;
  (void)width;
  (void)height;
}

void session_manager_get_vfb_size(unsigned int session_id,
                                  unsigned int *out_width,
                                  unsigned int *out_height) {
  (void)session_id;
  if (out_width) {
    *out_width = 320u;
  }
  if (out_height) {
    *out_height = 200u;
  }
}

size_t session_manager_read_vfb(unsigned int session_id,
                                unsigned char *out_pixels,
                                unsigned int x, unsigned int y,
                                unsigned int w, unsigned int h) {
  (void)session_id;
  (void)out_pixels;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  return 0u;
}

/* ------------------------------------------------------------------ */
/* event_bus.c (auth table backing store)                             */
/* ------------------------------------------------------------------ */

static pending_auth_request_t g_stub_auth_table[8];

pending_auth_request_t *event_get_pending_auth_table(void) {
  return g_stub_auth_table;
}

/* ------------------------------------------------------------------ */
/* serial_hal.c                                                       */
/* ------------------------------------------------------------------ */

void serial_hal_write(const char *message) { (void)message; }

/* ------------------------------------------------------------------ */
/* arch/x86/idt_stubs.asm — fault recovery shims (no-op on host)      */
/* ------------------------------------------------------------------ */

int fault_recover_set(void) { return 0; }
void fault_recover_clear(void) {}

/* ------------------------------------------------------------------ */
/* drivers/video/vga_gfx.c                                            */
/* ------------------------------------------------------------------ */

int vga_gfx_is_active(void) { return 0; }
int vga_gfx_leave(void) { return 0; }

/* ------------------------------------------------------------------ */
/* hal/mouse_hal.c                                                    */
/* ------------------------------------------------------------------ */

void mouse_hal_set_bounds(int width, int height) {
  (void)width;
  (void)height;
}

void mouse_hal_update(void) {}

void mouse_hal_get_state(mouse_state_t *out_state) {
  if (out_state) {
    mouse_state_t zero = {0};
    *out_state = zero;
  }
}

/* ------------------------------------------------------------------ */
/* console.c / input_hal.c                                            */
/* ------------------------------------------------------------------ */

int console_try_read_injected(char *out_char) {
  (void)out_char;
  return 0;
}

int input_hal_try_read_char(char *out_char) {
  (void)out_char;
  return 0;
}

/* ------------------------------------------------------------------ */
/* drivers/video/vga_gfx.c (extended)                                 */
/* ------------------------------------------------------------------ */

int vga_gfx_enter(void) { return 0; }
void vga_gfx_clear(unsigned char color) { (void)color; }
void vga_gfx_put_pixel(int x, int y, unsigned char color) {
  (void)x; (void)y; (void)color;
}
unsigned char vga_gfx_get_pixel(int x, int y) {
  (void)x; (void)y;
  return 0u;
}
void vga_gfx_draw_rect(int x, int y, int w, int h, unsigned char color) {
  (void)x; (void)y; (void)w; (void)h; (void)color;
}

/* ------------------------------------------------------------------ */
/* drivers/video/vga_text.c                                           */
/* ------------------------------------------------------------------ */

void vga_text_set_cursor(int col, int row) { (void)col; (void)row; }
void vga_text_putchar_at(int col, int row, char ch, unsigned char attr) {
  (void)col; (void)row; (void)ch; (void)attr;
}

/* ------------------------------------------------------------------ */
/* hal/video_hal.c                                                    */
/* ------------------------------------------------------------------ */

void video_hal_clear(void) {}

/* ------------------------------------------------------------------ */
/* session_manager.c (extended VFB / yield surface)                   */
/* ------------------------------------------------------------------ */

void session_manager_clear_vfb(unsigned int session_id) { (void)session_id; }

unsigned char *session_manager_get_vfb(unsigned int session_id) {
  (void)session_id;
  return NULL;
}

void session_manager_get_virtual_mouse(unsigned int session_id,
                                       int *out_x, int *out_y,
                                       unsigned char *out_buttons) {
  (void)session_id;
  if (out_x) *out_x = 0;
  if (out_y) *out_y = 0;
  if (out_buttons) *out_buttons = 0u;
}

int session_manager_tick_yield(void) { return 0; }

/* ------------------------------------------------------------------ */
/* clock/clock_service.c                                              */
/* ------------------------------------------------------------------ */

clock_result_t clock_service_get_epoch(uint32_t *out) {
  if (out) {
    *out = 0u;
  }
  return CLOCK_OK;
}

uint32_t clock_service_get_ticks(void) { return 0u; }

clock_result_t clock_service_set_time(unsigned int subject_id,
                                      const hal_time_t *new_time) {
  (void)subject_id;
  (void)new_time;
  return CLOCK_ERR_DENIED;
}

clock_result_t clock_service_get_time(hal_time_t *out) {
  if (out) {
    hal_time_t zero = {0};
    *out = zero;
  }
  return CLOCK_OK;
}

void clock_service_format_datetime(const hal_time_t *t, char *buf,
                                   unsigned int buf_size) {
  (void)t;
  if (buf && buf_size > 0u) {
    buf[0] = '\0';
  }
}
