/**
 * @file syscall_entry_stub_test.c
 * @brief Conformance test for the M1 syscall entry stub (issue #232).
 *
 * Three checks, mirroring the "Done when" list in #232:
 *
 *   1. All-vectors sweep: every vector in
 *      [SYSCALL_VECTOR_BASE, SYSCALL_VECTOR_LIMIT) — plus a handful of
 *      out-of-range vectors — returns IPC_ERR_INVALID_MSG.
 *   2. Deny-marker shape: the line emitted by `kernel_syscall_entry`
 *      passes `cap_deny_marker_validate()` and matches the canonical
 *      `CAP:DENY:<actor>:syscall:-\n` form.
 *   3. ABI anchor: `SYSCALL_ENTRY_ABI_ANCHOR` encodes the same
 *      `OS_ABI_VERSION` that `secureos_abi.h` advertises (same
 *      cross-check pattern used by #228 for manifest os_abi_version).
 *
 * Launched by:
 *   build/scripts/test_syscall_entry_stub.sh (dispatched via
 *   build/scripts/test.sh syscall_entry_stub).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/proc/syscall_entry.h"
#include "../user/include/secureos_abi.h"

static int g_failures = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:syscall_entry_stub:%s\n", reason);
  g_failures++;
}

/* Capture stdout for the duration of `body()` and return the captured
 * bytes in a static buffer. Returns the number of bytes captured (or
 * -1 on error). Uses freopen() to redirect the C stdio stream; works
 * on both glibc and musl. */
static char g_capture[4096];

static long capture_stdout(void (*body)(void *), void *ctx) {
  /* Use a tmpfile to avoid filesystem hygiene concerns. */
  fflush(stdout);
  FILE *saved = stdout;
  FILE *tmp = tmpfile();
  if (!tmp) return -1;

  /* Swap: redirect stdout to tmp by dup'ing the fd. We avoid freopen
   * because it would close the original stdout fd permanently. */
  int saved_fd = dup(fileno(saved));
  if (saved_fd < 0) { fclose(tmp); return -1; }
  fflush(saved);
  if (dup2(fileno(tmp), fileno(saved)) < 0) {
    close(saved_fd); fclose(tmp); return -1;
  }

  body(ctx);

  fflush(saved);
  /* Restore original stdout. */
  dup2(saved_fd, fileno(saved));
  close(saved_fd);

  /* Read tmp from the start. */
  fseek(tmp, 0L, SEEK_SET);
  size_t n = fread(g_capture, 1u, sizeof(g_capture) - 1u, tmp);
  g_capture[n] = '\0';
  fclose(tmp);
  return (long)n;
}

/* --- check 1: all-vectors sweep ---------------------------------- */

struct sweep_ctx {
  cap_subject_id_t subject;
  uint32_t vector;
  ipc_result_t result;
};

static void sweep_body(void *p) {
  struct sweep_ctx *c = (struct sweep_ctx *)p;
  c->result = kernel_syscall_entry(c->subject, c->vector, 0, 0, 0);
}

static void check_all_vectors_return_invalid_msg(void) {
  /* In-range. */
  for (uint32_t v = SYSCALL_VECTOR_BASE; v < SYSCALL_VECTOR_LIMIT; ++v) {
    struct sweep_ctx ctx = { .subject = 7u, .vector = v, .result = IPC_OK };
    long n = capture_stdout(sweep_body, &ctx);
    if (n < 0) { fail("sweep_capture_failed"); return; }
    if (ctx.result != IPC_ERR_INVALID_MSG) {
      fail("in_range_vector_returned_non_invalid_msg");
      return;
    }
  }
  /* Out-of-range probes — also must reject. */
  const uint32_t probes[] = { SYSCALL_VECTOR_LIMIT,
                              SYSCALL_VECTOR_LIMIT + 1u,
                              0xFFFFFFFFu,
                              0x10000u };
  for (size_t i = 0; i < sizeof(probes)/sizeof(probes[0]); ++i) {
    struct sweep_ctx ctx = { .subject = 7u, .vector = probes[i], .result = IPC_OK };
    long n = capture_stdout(sweep_body, &ctx);
    if (n < 0) { fail("sweep_capture_failed"); return; }
    if (ctx.result != IPC_ERR_INVALID_MSG) {
      fail("out_of_range_vector_returned_non_invalid_msg");
      return;
    }
  }
  printf("TEST:PASS:syscall_entry_stub_invalid_msg_sweep\n");
}

/* --- check 2: deny-marker shape conformance ---------------------- */

static void single_call_body(void *p) {
  struct sweep_ctx *c = (struct sweep_ctx *)p;
  c->result = kernel_syscall_entry(c->subject, c->vector, 0, 0, 0);
}

static void check_deny_marker_shape(void) {
  struct sweep_ctx ctx = { .subject = 42u, .vector = SYSCALL_VECTOR_BASE, .result = IPC_OK };
  long n = capture_stdout(single_call_body, &ctx);
  if (n < 0) { fail("deny_capture_failed"); return; }
  if (ctx.result != IPC_ERR_INVALID_MSG) {
    fail("deny_call_did_not_return_invalid_msg");
    return;
  }
  /* Exactly one CAP:DENY line. */
  const char *expected = "CAP:DENY:42:syscall:-\n";
  if (strcmp(g_capture, expected) != 0) {
    printf("TEST:FAIL:syscall_entry_stub:deny_line_mismatch:got=[%s]:expected=[%s]\n",
           g_capture, expected);
    g_failures++;
    return;
  }
  /* Cross-validate with the canonical validator (single source of truth
   * for the marker grammar — same as #211). */
  char reason[64] = {0};
  int rc = cap_deny_marker_validate(g_capture, reason, sizeof(reason));
  if (rc != 0) {
    printf("TEST:FAIL:syscall_entry_stub:deny_validate_rejected:%s\n", reason);
    g_failures++;
    return;
  }
  printf("TEST:PASS:syscall_entry_stub_deny_marker_shape\n");
}

/* --- check 3: ABI anchor cross-check ------------------------------ */

static void check_abi_anchor(void) {
  /* SYSCALL_ENTRY_ABI_ANCHOR = (OS_ABI_VERSION << 16) | SYSCALL_VECTOR_COUNT. */
  uint32_t expected = ((uint32_t)OS_ABI_VERSION << 16) | SYSCALL_VECTOR_COUNT;
  if (SYSCALL_ENTRY_ABI_ANCHOR != expected) {
    fail("abi_anchor_mismatch");
    return;
  }
  /* And the OS_ABI_VERSION half must round-trip cleanly. */
  uint32_t recovered = (uint32_t)(SYSCALL_ENTRY_ABI_ANCHOR >> 16);
  if (recovered != (uint32_t)OS_ABI_VERSION) {
    fail("abi_anchor_version_recovery");
    return;
  }
  if (SYSCALL_VECTOR_COUNT == 0u) {
    fail("vector_count_zero");
    return;
  }
  printf("TEST:PASS:syscall_entry_stub_abi_anchor\n");
}

int main(void) {
  check_all_vectors_return_invalid_msg();
  check_deny_marker_shape();
  check_abi_anchor();

  if (g_failures != 0) {
    printf("TEST:FAIL:syscall_entry_stub:summary_failures=%d\n", g_failures);
    return 1;
  }
  printf("TEST:PASS:syscall_entry_stub\n");
  return 0;
}
