/**
 * @file native_fs_bytes_wrapper_test.c
 * @brief Host round-trip for the binary-safe fs wrappers (DEMO-01 #765).
 *
 * Purpose:
 *   Dynamic validation of `os_fs_read_file_bytes` / `os_fs_write_file_bytes`
 *   in `user/runtime/secureos_api_stubs.c`. The test maps a synthetic
 *   native bridge page at SECUREOS_NATIVE_BRIDGE_ADDR (same pattern as
 *   tests/process_exit_qemu_test.c) and drives the wrappers through:
 *     - the v5 version handshake (a v4 bridge page must degrade to the
 *       no-bridge fall-through — pinning the additive-slot gate),
 *     - full payload round-trips with embedded NUL bytes (byte lengths
 *       must cross the seam exactly; no strnlen-style shrink),
 *     - every return-code mapping (0->OK, 1->DENIED, 2->NOT_FOUND,
 *       3->ERROR),
 *     - argument guards that reject bad calls BEFORE touching the bridge
 *       (the only safe no-bridge dynamic path),
 *     - append-flag pass-through.
 *
 * Interactions:
 *   - user/runtime/secureos_api_stubs.c: implementation under test.
 *   - user/include/secureos_api.h: prototypes + contract comments.
 *
 * Launched by:
 *   build/scripts/test_native_fs_bytes_wrapper.sh
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "secureos_api.h"

enum {
  TEST_BRIDGE_MAGIC = 0x53524247u,
  TEST_BRIDGE_VERSION = 5u,
  TEST_BRIDGE_ADDR = 0x009FF000u,
};

/* Mirror of the runtime bridge shape in secureos_api_stubs.c: every
 * slot up to and including the DEMO-01 (#765) byte-I/O pair. */
typedef struct {
  unsigned int magic;
  unsigned int version;
  unsigned int reserved0;
  unsigned int reserved1;
  int (*console_write)(const char *message);
  int (*get_args)(char *out_buffer, unsigned int out_buffer_size);
  int (*net_device_ready)(void);
  int (*net_device_backend)(char *out_buffer, unsigned int out_buffer_size);
  int (*net_device_get_mac)(unsigned char *out_buffer,
                            unsigned int out_buffer_size);
  int (*net_frame_send)(const unsigned char *frame, unsigned int frame_len);
  int (*net_frame_recv)(unsigned char *out_buffer, unsigned int out_buffer_size,
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
                             unsigned int out_buffer_size,
                             unsigned int *out_len);
  int (*session_write_input)(unsigned int session_id, const char *input,
                             unsigned int len);
  int (*session_tick)(unsigned int session_id);
  int (*auth_poll_prompt)(void *out_prompt);
  int (*auth_respond)(unsigned int slot_index, int response);
  int (*session_read_framebuffer)(unsigned int session_id,
                                  unsigned char *out_pixels, unsigned int x,
                                  unsigned int y, unsigned int w,
                                  unsigned int h);
  int (*session_get_gfx_mode)(unsigned int session_id, int *out_mode);
  int (*session_set_wm_managed)(unsigned int session_id, int managed);
  int (*session_set_vfb_size)(unsigned int session_id, unsigned int width,
                              unsigned int height);
  int (*session_get_vfb_size)(unsigned int session_id, unsigned int *out_width,
                              unsigned int *out_height);
  int (*session_set_virtual_mouse)(unsigned int session_id, int x, int y,
                                   unsigned char buttons);
  int (*mouse_enable)(void);
  int (*mouse_disable)(void);
  int (*fs_read_file)(const char *path, char *out_buffer,
                      unsigned int out_buffer_size);
  int (*fs_write_file)(const char *path, const char *content, int append);
  int (*fs_list_dir)(const char *path, char *out_buffer,
                     unsigned int out_buffer_size);
  int (*fs_mkdir)(const char *path);
  int (*env_get)(const char *key, char *out_buffer,
                 unsigned int out_buffer_size);
  int (*env_set)(const char *key, const char *value);
  int (*env_list)(char *out_buffer, unsigned int out_buffer_size);
  int (*process_getcwd)(char *out_buffer, unsigned int out_buffer_size);
  int (*process_chdir)(const char *path);
  void (*process_exit)(int status);
  int (*process_spawn)(const char *path, const char *raw_args,
                       unsigned int flags, int *out_exit_status);
  int (*mem_brk)(int delta, void **out_prev_break);
  int (*fs_read_file_bytes)(const char *path, void *out_buffer,
                            unsigned int out_buffer_size,
                            unsigned int *out_len);
  int (*fs_write_file_bytes)(const char *path, const void *content,
                             unsigned int content_len, int append);
} test_native_bridge_t;

/* Mock backend state: canned bridge return codes + last-write capture. */
static unsigned int g_read_rc = 0u;
static unsigned int g_read_len = 0u;
static const unsigned char *g_read_payload = 0;
static unsigned int g_write_rc = 0u;
static char g_last_write_path[32] = {0};
static unsigned char g_last_write_bytes[64];
static unsigned int g_last_write_len = 0u;
static int g_last_write_append = -1;
static int g_read_calls = 0;
static int g_write_calls = 0;

/* Payload with leading, interior, and trailing NUL bytes. */
static const unsigned char k_payload[7] = {0, 'A', 0, 0, 'B', 'C', 0};

static int mock_fs_read_file_bytes(const char *path, void *out_buffer,
                                   unsigned int out_buffer_size,
                                   unsigned int *out_len) {
  (void)path;
  ++g_read_calls;
  if (g_read_rc != 0u) {
    return (int)g_read_rc;
  }
  if (g_read_len > out_buffer_size) {
    return 3; /* capacity failure, mirrors kernel FS_ERR_NO_SPACE mapping */
  }
  memcpy(out_buffer, g_read_payload, g_read_len);
  *out_len = g_read_len;
  return 0;
}

static int mock_fs_write_file_bytes(const char *path, const void *content,
                                    unsigned int content_len, int append) {
  ++g_write_calls;
  snprintf(g_last_write_path, sizeof(g_last_write_path), "%s",
           path ? path : "");
  if (content_len > sizeof(g_last_write_bytes)) {
    content_len = (unsigned int)sizeof(g_last_write_bytes);
  }
  memcpy(g_last_write_bytes, content, content_len);
  g_last_write_len = content_len;
  g_last_write_append = append;
  return (int)g_write_rc;
}

static int g_failures = 0;

static void record_check(int ok, const char *name) {
  if (ok) {
    printf("TEST:PASS:native_fs_bytes_wrapper:%s\n", name);
  } else {
    printf("TEST:FAIL:native_fs_bytes_wrapper:%s\n", name);
    ++g_failures;
  }
}

static test_native_bridge_t *map_bridge(unsigned int version) {
  void *base = mmap((void *)(unsigned long)TEST_BRIDGE_ADDR,
                    sizeof(test_native_bridge_t), PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  test_native_bridge_t *bridge;

  if (base == MAP_FAILED) {
    printf("TEST:FAIL:native_fs_bytes_wrapper:bridge_map_failed:%d\n", errno);
    return 0;
  }
  bridge = (test_native_bridge_t *)base;
  memset(bridge, 0, sizeof(*bridge));
  bridge->magic = TEST_BRIDGE_MAGIC;
  bridge->version = version;
  bridge->fs_read_file_bytes = mock_fs_read_file_bytes;
  bridge->fs_write_file_bytes = mock_fs_write_file_bytes;
  return bridge;
}

static void unmap_bridge(test_native_bridge_t *bridge) {
  if (bridge != 0) {
    (void)munmap((void *)bridge, sizeof(*bridge));
  }
}

static void mock_reset(void) {
  g_read_rc = 0u;
  g_read_len = 0u;
  g_read_payload = k_payload;
  g_write_rc = 0u;
  g_read_calls = 0;
  g_write_calls = 0;
  g_last_write_len = 0u;
  g_last_write_append = -1;
  g_last_write_path[0] = '\0';
  memset(g_last_write_bytes, 0, sizeof(g_last_write_bytes));
}

int main(void) {
  test_native_bridge_t *bridge;
  unsigned char buf[16];
  unsigned int out_len = 0xFFFFFFFFu;
  os_status_t rc;

  printf("TEST:START:native_fs_bytes_wrapper\n");

  /* --- Argument guards fire before any bridge access (safe no-bridge) --- */
  rc = os_fs_read_file_bytes("/x", buf, sizeof(buf), NULL);
  record_check(rc == OS_STATUS_ERROR, "read_null_out_len_early_error");

  rc = os_fs_read_file_bytes("/x", buf, 0u, &out_len);
  record_check(rc == OS_STATUS_ERROR, "read_zero_capacity_early_error");

  rc = os_fs_read_file_bytes(NULL, buf, sizeof(buf), &out_len);
  record_check(rc == OS_STATUS_ERROR, "read_null_path_early_error");

  rc = os_fs_write_file_bytes("/x", NULL, 4u, 0);
  record_check(rc == OS_STATUS_ERROR, "write_null_content_early_error");

  rc = os_fs_write_file_bytes(NULL, k_payload, sizeof(k_payload), 0);
  record_check(rc == OS_STATUS_ERROR, "write_null_path_early_error");

  /* --- Version handshake: a pre-v5 bridge page must degrade --- */
  bridge = map_bridge(4u);
  if (bridge == 0) {
    return 1;
  }
  mock_reset();
  g_read_payload = k_payload;
  g_read_len = 3u;
  out_len = 0xFFFFFFFFu;
  rc = os_fs_read_file_bytes("/old.bin", buf, sizeof(buf), &out_len);
  record_check(rc == OS_STATUS_OK && out_len == 0u && g_read_calls == 0,
               "pre_v5_bridge_degrades_to_no_bridge_read");

  rc = os_fs_write_file_bytes("/old.bin", k_payload, sizeof(k_payload), 0);
  record_check(rc == OS_STATUS_OK && g_write_calls == 0,
               "pre_v5_bridge_degrades_to_no_bridge_write");
  unmap_bridge(bridge);

  /* --- Live v5 bridge: exact byte round-trips and status mapping ------ */
  bridge = map_bridge(TEST_BRIDGE_VERSION);
  if (bridge == 0) {
    return 1;
  }

  /* Read OK: 7 bytes with embedded NULs survive verbatim. */
  mock_reset();
  g_read_payload = k_payload;
  g_read_len = sizeof(k_payload);
  memset(buf, 0xAA, sizeof(buf));
  out_len = 0u;
  rc = os_fs_read_file_bytes("/payload.bin", buf, sizeof(buf), &out_len);
  record_check(rc == OS_STATUS_OK && out_len == sizeof(k_payload) &&
                   memcmp(buf, k_payload, sizeof(k_payload)) == 0,
               "read_exact_bytes_with_nuls");

  /* Capacity failure maps to ERROR and zeroes the length. */
  mock_reset();
  g_read_payload = k_payload;
  g_read_len = sizeof(k_payload);
  out_len = 7u;
  rc = os_fs_read_file_bytes("/payload.bin", buf, 4u, &out_len);
  record_check(rc == OS_STATUS_ERROR && out_len == 0u,
               "read_capacity_denied_maps_error");

  /* NOT_FOUND (rc 2) maps to NOT_FOUND. */
  mock_reset();
  g_read_rc = 2u;
  out_len = 9u;
  rc = os_fs_read_file_bytes("/missing.bin", buf, sizeof(buf), &out_len);
  record_check(rc == OS_STATUS_NOT_FOUND && out_len == 0u,
               "read_rc2_maps_not_found");

  /* Denied (rc 1) maps to DENIED, buffer untouched. */
  mock_reset();
  g_read_rc = 1u;
  rc = os_fs_read_file_bytes("/denied.bin", buf, sizeof(buf), &out_len);
  record_check(rc == OS_STATUS_DENIED, "read_rc1_maps_denied");

  /* Write OK: exact length + embedded NULs + path + append flag land. */
  mock_reset();
  rc = os_fs_write_file_bytes("/out.bin", k_payload, sizeof(k_payload), 0);
  record_check(rc == OS_STATUS_OK && g_write_calls == 1 &&
                   g_last_write_len == sizeof(k_payload) &&
                   memcmp(g_last_write_bytes, k_payload,
                          sizeof(k_payload)) == 0 &&
                   strcmp(g_last_write_path, "/out.bin") == 0 &&
                   g_last_write_append == 0,
               "write_exact_bytes_append0");

  mock_reset();
  rc = os_fs_write_file_bytes("/log.bin", "tail", 4u, 1);
  record_check(rc == OS_STATUS_OK && g_last_write_len == 4u &&
                   g_last_write_append == 1 &&
                   memcmp(g_last_write_bytes, "tail", 4) == 0,
               "write_append_flag_passthrough");

  /* Zero-length write still carries a valid buffer + creates empty file. */
  mock_reset();
  rc = os_fs_write_file_bytes("/empty.bin", k_payload, 0u, 0);
  record_check(rc == OS_STATUS_OK && g_last_write_len == 0u,
               "write_zero_len_ok");

  /* Denied (rc 1) maps to DENIED; other (rc 3) to ERROR. */
  mock_reset();
  g_write_rc = 1u;
  rc = os_fs_write_file_bytes("/nope.bin", k_payload, 3u, 0);
  record_check(rc == OS_STATUS_DENIED, "write_rc1_maps_denied");

  mock_reset();
  g_write_rc = 3u;
  rc = os_fs_write_file_bytes("/nope.bin", k_payload, 3u, 0);
  record_check(rc == OS_STATUS_ERROR, "write_rc3_maps_error");

  unmap_bridge(bridge);

  if (g_failures == 0) {
    printf("TEST:PASS:native_fs_bytes_wrapper\n");
    return 0;
  }

  printf("TEST:FAIL:native_fs_bytes_wrapper:failures=%d\n", g_failures);
  return 1;
}
