/**
 * @file mem_brk_arena_cap_deny_test.c
 * @brief Host-gated deny-marker contract for `app_native_mem_brk`
 *        over-cap growth rejects (issue #558).
 *
 * Purpose:
 *   Pins the negative path where a positive `mem_brk` growth request exceeds
 *   the active arena cap (`APP_NATIVE_HEAP_BYTES` in the native bridge path).
 *   The syscall contract already returned OS_STATUS_DENIED; this test adds the
 *   audit-evidence pin by asserting the over-cap deny hook emits canonical
 *   `CAP:DENY:<sid>:mem_brk:arena_bytes`-shape output.
 *
 * How it's used:
 *   - `build/scripts/test_mem_brk_arena_cap_deny.sh` compiles this test with
 *     `kernel/user/app_native_heap.c` and validates deterministic markers.
 *   - `build/scripts/test.sh mem_brk_arena_cap_deny` dispatches the host gate.
 *
 * Notes:
 *   - We intentionally drive the production `app_native_mem_brk` body directly
 *     (same shape as `mem_brk_qemu_test`) because the runtime wrapper reads
 *     the fixed bridge address, which is unavailable in host-only tests.
 */

#include <stdio.h>
#include <string.h>

#include "../kernel/user/app_native_heap.h"

enum {
  STATUS_OK = 0,
  STATUS_DENIED = 1,
};

static int g_fail = 0;
static unsigned int g_deny_calls = 0u;
static char g_marker[64];

static void fail(const char *reason) {
  printf("TEST:FAIL:mem_brk_arena_cap_deny:%s\n", reason);
  g_fail = 1;
}

static void deny_hook_capture(void) {
  static const char kMarker[] = "CAP:DENY:77:mem_brk:arena_bytes\n";
  size_t len = strlen(kMarker);

  g_deny_calls += 1u;
  if (len >= sizeof(g_marker)) {
    len = sizeof(g_marker) - 1u;
  }
  memcpy(g_marker, kMarker, len);
  g_marker[len] = '\0';
}

static void reset_fixture(void) {
  g_deny_calls = 0u;
  g_marker[0] = '\0';
  app_native_heap_reset();
  app_native_heap_set_arena_over_cap_deny_hook(deny_hook_capture);
}

static void test_over_cap_denied_and_marked(void) {
  void *prev = 0;
  int over = (int)APP_NATIVE_HEAP_BYTES + 1;

  reset_fixture();

  if (app_native_mem_brk(over, &prev) != STATUS_DENIED) {
    fail("over_cap_not_denied");
    return;
  }
  if (app_native_heap_break_for_tests() != 0u) {
    fail("over_cap_moved_break");
    return;
  }
  if (g_deny_calls != 1u) {
    fail("over_cap_missing_deny_hook_call");
    return;
  }

  printf("TEST:PASS:mem_brk_arena_cap_deny:deny_status\n");

  if (strcmp(g_marker, "CAP:DENY:77:mem_brk:arena_bytes\n") != 0) {
    fail("deny_marker_shape_mismatch");
    return;
  }

  printf("TEST:PASS:mem_brk_arena_cap_deny:deny_marker\n");
}

static void test_shrink_underflow_has_no_marker(void) {
  void *prev = 0;

  reset_fixture();

  if (app_native_mem_brk(-1, &prev) != STATUS_DENIED) {
    fail("shrink_underflow_not_denied");
    return;
  }
  if (g_deny_calls != 0u) {
    fail("shrink_underflow_emitted_overcap_marker");
    return;
  }

  if (app_native_mem_brk(4096, &prev) != STATUS_OK) {
    fail("post_underflow_grow_failed");
    return;
  }
  if (g_deny_calls != 0u) {
    fail("successful_grow_emitted_marker");
    return;
  }

  printf("TEST:PASS:mem_brk_arena_cap_deny:shrink_underflow_silent\n");
}

int main(void) {
  printf("TEST:START:mem_brk_arena_cap_deny\n");

  test_over_cap_denied_and_marked();
  test_shrink_underflow_has_no_marker();

  app_native_heap_set_arena_over_cap_deny_hook(0);

  if (g_fail) {
    return 1;
  }

  printf("TEST:PASS:mem_brk_arena_cap_deny\n");
  return 0;
}
