/**
 * @file process_spawn_argv_roundtrip_test.c
 * @brief Host-side contract pin for `os_process_spawn` argv marshalling and
 *        `out_exit_status` propagation (issue #546).
 *
 * Purpose:
 *   Lock the current v0 wrapper behavior documented in docs/abi/syscalls.md:
 *   `argv[1..]` is space-joined into launcher `raw_args` and passed through
 *   the native bridge `process_spawn` slot, and a successful return writes the
 *   child exit code via `out_exit_status`.
 *
 *   This test maps a synthetic bridge page at SECUREOS_NATIVE_BRIDGE_ADDR,
 *   installs a mock process_spawn callback, and asserts:
 *     1) N=3 and N=5 argv vectors are joined deterministically,
 *     2) an argv element containing an internal space is not rejected and is
 *        preserved in the joined string (documented v0 limitation),
 *     3) `OS_STATUS_OK` return is distinct from propagated child exit status
 *        (`*out_exit_status = 42`).
 *
 * Interactions:
 *   - user/runtime/secureos_api_stubs.c: os_process_spawn wrapper under test.
 *   - user/include/secureos_api.h: API shape + status enum.
 *   - docs/abi/syscalls.md: normative wording for v0 marshalling caveat.
 *
 * Launched by:
 *   build/scripts/test_process_spawn_argv_roundtrip.sh
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "../user/include/secureos_api.h"

enum {
  TEST_BRIDGE_MAGIC = 0x53524247u,
  TEST_BRIDGE_VERSION = 4u,
  TEST_BRIDGE_ADDR = 0x009FF000u,
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

static const char *g_expect_path = 0;
static const char *g_expect_raw_args = 0;
static int g_spawn_calls = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:process_spawn_argv_roundtrip:%s\n", reason);
  exit(1);
}

static int mock_process_spawn(const char *path,
                              const char *raw_args,
                              unsigned int flags,
                              int *out_exit_status) {
  g_spawn_calls += 1;

  if (path == 0 || g_expect_path == 0 || strcmp(path, g_expect_path) != 0) {
    return 3;
  }
  if (raw_args == 0 || g_expect_raw_args == 0 || strcmp(raw_args, g_expect_raw_args) != 0) {
    return 3;
  }
  if (flags != 0u) {
    return 3;
  }

  if (out_exit_status != 0) {
    *out_exit_status = 42;
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
    printf("TEST:FAIL:process_spawn_argv_roundtrip:bridge_map_failed:%d\n", errno);
    return 0;
  }

  bridge = (test_native_bridge_t *)base;
  memset(bridge, 0, sizeof(*bridge));
  bridge->magic = TEST_BRIDGE_MAGIC;
  bridge->version = TEST_BRIDGE_VERSION;
  bridge->process_spawn = mock_process_spawn;
  return bridge;
}

static void unmap_bridge(test_native_bridge_t *bridge) {
  if (bridge != 0) {
    (void)munmap((void *)bridge, sizeof(*bridge));
  }
}

static void assert_spawn_ok(const char *sub_marker,
                            const char *path,
                            const char *const *argv,
                            const char *expect_raw) {
  int child_status = -7;
  os_status_t rc;

  g_expect_path = path;
  g_expect_raw_args = expect_raw;
  g_spawn_calls = 0;

  rc = os_process_spawn(path, argv, 0u, &child_status);
  if (rc != OS_STATUS_OK) {
    fail("spawn_status_not_ok");
  }
  if (g_spawn_calls != 1) {
    fail("spawn_callback_not_called_once");
  }
  if (child_status != 42) {
    fail("out_exit_status_not_propagated");
  }

  printf("TEST:PASS:process_spawn_argv_roundtrip:%s\n", sub_marker);
}

int main(void) {
  test_native_bridge_t *bridge;

  printf("TEST:START:process_spawn_argv_roundtrip\n");

  bridge = map_bridge();
  if (bridge == 0) {
    return 1;
  }

  {
    const char *argv_n3[] = {"cc", "/apps/dev/hello.c", "-E", 0};
    assert_spawn_ok("argv_n3_roundtrip",
                    "/apps/dev/cc",
                    argv_n3,
                    "/apps/dev/hello.c -E");
  }

  {
    const char *argv_n5[] = {
      "cc",
      "/apps/dev/hello.c",
      "-o",
      "/apps/hello.bin",
      "-I/apps/dev/include",
      0};
    assert_spawn_ok("argv_n5_roundtrip",
                    "/apps/dev/cc",
                    argv_n5,
                    "/apps/dev/hello.c -o /apps/hello.bin -I/apps/dev/include");
  }

  {
    const char *argv_space[] = {
      "cc",
      "/home/my file.c",
      "-o",
      "/apps/hello.bin",
      0};
    assert_spawn_ok("space_join_limitation_pinned",
                    "/apps/dev/cc",
                    argv_space,
                    "/home/my file.c -o /apps/hello.bin");
  }

  printf("TEST:PASS:process_spawn_argv_roundtrip:out_exit_status_roundtrip\n");

  unmap_bridge(bridge);
  printf("TEST:PASS:process_spawn_argv_roundtrip\n");
  return 0;
}
