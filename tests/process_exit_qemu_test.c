/**
 * @file process_exit_qemu_test.c
 * @brief Issue #551 starter gate: bridge-level exit-status round-trip pin
 *        for `os_process_exit` -> `os_process_spawn`.
 *
 * Purpose:
 *   Provide a deterministic host-side preflight while the full launcher/
 *   QEMU end-to-end harness is being built. This test maps a synthetic
 *   native bridge page at SECUREOS_NATIVE_BRIDGE_ADDR and validates two
 *   contracts that #551 depends on:
 *
 *   1) `os_process_exit(0x42)` reaches the bridge callback and carries
 *      the exact status value.
 *   2) A subsequent successful `os_process_spawn(..., out_exit_status)`
 *      can round-trip that captured status as `0x42`.
 *
 *   The bridge callback uses `longjmp` to emulate the launcher's
 *   no-return recovery handoff (`fault_recover_jump`).
 *
 * Interactions:
 *   - user/runtime/secureos_api_stubs.c: wrappers under test
 *     (`os_process_exit`, `os_process_spawn`).
 *   - user/include/secureos_api.h: API signatures + status enum.
 *
 * Launched by:
 *   build/scripts/test_process_exit_qemu.sh
 */

#define _GNU_SOURCE 1

#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../user/include/secureos_api.h"

enum {
  TEST_BRIDGE_MAGIC = 0x53524247u,
  TEST_BRIDGE_VERSION = 4u,
  TEST_BRIDGE_ADDR = 0x009FF000u,
};

/* Mirror of the runtime bridge shape in secureos_api_stubs.c. */
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
  int (*fs_read_file)(const char *path, char *out_buffer, unsigned int out_buffer_size);
  int (*fs_write_file)(const char *path, const char *content, int append);
  int (*fs_list_dir)(const char *path, char *out_buffer, unsigned int out_buffer_size);
  int (*fs_mkdir)(const char *path);
  int (*env_get)(const char *key, char *out_buffer, unsigned int out_buffer_size);
  int (*env_set)(const char *key, const char *value);
  int (*env_list)(char *out_buffer, unsigned int out_buffer_size);
  int (*process_getcwd)(char *out_buffer, unsigned int out_buffer_size);
  int (*process_chdir)(const char *path);
  void (*process_exit)(int status);
  int (*process_spawn)(const char *path,
                       const char *raw_args,
                       unsigned int flags,
                       int *out_exit_status);
} test_native_bridge_t;

static jmp_buf g_exit_recover;
static int g_exit_calls = 0;
static int g_last_exit_status = -1;

static void fail(const char *reason) {
  printf("TEST:FAIL:process_exit_qemu:%s\n", reason);
  exit(1);
}

static void mock_process_exit(int status) {
  g_exit_calls += 1;
  g_last_exit_status = status;
  longjmp(g_exit_recover, 1);
}

static int mock_process_spawn(const char *path,
                              const char *raw_args,
                              unsigned int flags,
                              int *out_exit_status) {
  if (path == 0 || path[0] == '\0') {
    return 3;
  }
  if (raw_args == 0) {
    return 3;
  }
  if (flags != 0u) {
    return 3;
  }
  if (out_exit_status != 0) {
    *out_exit_status = g_last_exit_status;
  }
  return 0;
}

static test_native_bridge_t *map_bridge(void) {
  void *base = mmap((void *)(unsigned long)TEST_BRIDGE_ADDR,
                    sizeof(test_native_bridge_t),
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                    -1,
                    0);
  test_native_bridge_t *bridge;

  if (base == MAP_FAILED) {
    printf("TEST:FAIL:process_exit_qemu:bridge_map_failed:%d\n", errno);
    return 0;
  }

  bridge = (test_native_bridge_t *)base;
  memset(bridge, 0, sizeof(*bridge));
  bridge->magic = TEST_BRIDGE_MAGIC;
  bridge->version = TEST_BRIDGE_VERSION;
  bridge->process_exit = mock_process_exit;
  bridge->process_spawn = mock_process_spawn;
  return bridge;
}

static void unmap_bridge(test_native_bridge_t *bridge) {
  if (bridge != 0) {
    (void)munmap((void *)bridge, sizeof(*bridge));
  }
}

int main(void) {
  test_native_bridge_t *bridge;
  int jumped;

  printf("TEST:START:process_exit_qemu\n");

  bridge = map_bridge();
  if (bridge == 0) {
    return 1;
  }

  jumped = setjmp(g_exit_recover);
  if (jumped == 0) {
    os_status_t rc = os_process_exit(0x42);
    if (rc != OS_STATUS_OK) {
      fail("exit_wrapper_no_bridge_status_drift");
    }
    fail("exit_callback_did_not_nonreturn");
  }

  if (g_exit_calls != 1) {
    fail("exit_callback_call_count_mismatch");
  }
  if (g_last_exit_status != 0x42) {
    fail("exit_status_not_captured_0x42");
  }
  printf("TEST:PASS:process_exit_qemu:exit_bridge_invoked_0x42\n");

  {
    const char *argv[] = {"child", "--probe", 0};
    int child_status = -1;
    os_status_t rc = os_process_spawn("/apps/child", argv, 0u, &child_status);
    if (rc != OS_STATUS_OK) {
      fail("spawn_status_not_ok");
    }
    if (child_status != 0x42) {
      fail("spawn_out_exit_status_not_0x42");
    }
  }
  printf("TEST:PASS:process_exit_qemu:roundtrip_status_0x42\n");

  unmap_bridge(bridge);
  printf("TEST:PASS:process_exit_qemu\n");
  return 0;
}
