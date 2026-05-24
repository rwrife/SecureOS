/**
 * @file ipc_bounds_test.c
 * @brief End-to-end host test for the M1 IPC address_space bounds
 *        enforcement (issue #260, plan #198 slice 2).
 *
 * Wires together:
 *   - kernel/proc/process.c                  (process table + aspace bind)
 *   - kernel/proc/address_space.c            (aspace_partition / contains)
 *   - kernel/ipc/ipc_ops.c                   (ipc_send / ipc_recv)
 *   - kernel/cap/cap_deny_marker.c           (canonical marker formatter)
 *
 * Covers the done-when bullets from issue #260 IPC half:
 *
 *   1. Allow case: in-window send + recv succeeds. The previously-existing
 *      ipc_sync_v0 / ipc_handle_gate tests continue to pass (no PCB =>
 *      check skipped); this test additionally proves that *with* a live
 *      PCB whose aspace contains the envelope buffer, the allow path
 *      still completes. Emits TEST:PASS:ipc_bounds_allow.
 *
 *   2. Deny case: send with a buffer one byte past base+size returns
 *      IPC_ERR_BOUNDS and emits exactly one canonical deny marker
 *      CAP:DENY:<sender>:ipc_send:bounds. Emits
 *      TEST:PASS:ipc_bounds_deny_one_past_end.
 *
 *   3. Deny case: send with a buffer that straddles the boundary
 *      (base + size - 4, claimed length spanning past base + size) is
 *      rejected with IPC_ERR_BOUNDS. Emits
 *      TEST:PASS:ipc_bounds_deny_straddle.
 *
 *   4. Backward-compat carve-out: a sender with NO live PCB (legacy
 *      cap_subject_id_t in the ipc_sync_v0 harness shape) is NOT
 *      subject to the bounds check; an out-of-window buffer still
 *      succeeds because there is no window to escape from. Emits
 *      TEST:PASS:ipc_bounds_no_pcb_skipped.
 *
 *   5. Aggregate marker TEST:PASS:ipc_bounds is the rollup the harness
 *      asserts, matching the per-target naming convention.
 *
 * Launched by:
 *   build/scripts/test_ipc_bounds.sh (dispatched via test.sh and
 *   validate_bundle.sh).
 *
 * Issue: #260.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/address_space.h"
#include "../kernel/proc/process.h"

/* Arena big enough to partition into 2 windows that each satisfy
 * aspace_window_min_bytes(). 256 KiB total → 128 KiB / window. */
#define ARENA_BYTES (256u * 1024u)
static uint8_t g_arena[ARENA_BYTES] __attribute__((aligned(64)));

static int g_fail_count = 0;

#define CHECK(cond, label)                                              \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "TEST:FAIL:ipc_bounds:%s\n", (label));           \
      g_fail_count++;                                                   \
    }                                                                   \
  } while (0)

/* Build an ipc_msg_v0 in `dst` with a known payload byte. */
static void fill_envelope(ipc_msg_v0 *dst, uint8_t marker) {
  memset(dst, 0, sizeof(*dst));
  dst->abi_version = (uint16_t)OS_ABI_VERSION;
  dst->flags = 0u;
  dst->sender_subject = 0u;  /* kernel will stamp on send */
  dst->tag = 0u;
  dst->payload_len = 1u;
  dst->payload[0] = marker;
}

/* Reset all global tables to a known-empty state at the top of each case. */
static void reset_all(void) {
  process_table_reset();
  ipc_port_table_reset();
  cap_table_reset();
}

/* Count occurrences of a literal needle in a temp-redirected stdout file. */
static int count_marker_in_file(const char *path, const char *needle) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  char buf[8192];
  size_t n = fread(buf, 1u, sizeof(buf) - 1u, f);
  fclose(f);
  buf[n] = '\0';
  int count = 0;
  const char *p = buf;
  size_t nlen = strlen(needle);
  while ((p = strstr(p, needle)) != NULL) {
    count++;
    p += nlen;
  }
  return count;
}

/* Capture stdout while running `body`, then restore. Writes captured
 * bytes to `tmp_path`. Returns 0 on success. */
static int capture_stdout(const char *tmp_path, void (*body)(void *), void *arg) {
  fflush(stdout);
  int saved = dup(fileno(stdout));
  if (saved < 0) {
    return -1;
  }
  FILE *cap = freopen(tmp_path, "w+", stdout);
  if (cap == NULL) {
    return -1;
  }
  body(arg);
  fflush(stdout);
  /* Restore stdout. */
  dup2(saved, fileno(stdout));
  close(saved);
  /* `cap` was redirected via freopen; the FILE* now points back at
   * the original fd. Reopening tmp_path explicitly would be cleaner
   * but is unnecessary — we only need the file contents. */
  return 0;
}

/* --- Test 1: allow path with live PCB --- */
typedef struct {
  cap_subject_id_t sender;
  cap_subject_id_t receiver;
  ipc_port_t port;
  ipc_msg_v0 *out_buf;
  ipc_msg_v0 *in_buf;
  ipc_result_t send_rc;
  ipc_result_t recv_rc;
} send_recv_ctx_t;

static void do_send(void *p) {
  send_recv_ctx_t *c = (send_recv_ctx_t *)p;
  c->send_rc = ipc_send(c->sender, c->port, c->out_buf);
}

static void do_recv(void *p) {
  send_recv_ctx_t *c = (send_recv_ctx_t *)p;
  c->recv_rc = ipc_recv(c->receiver, c->port, c->in_buf);
}

static void test_allow_in_window(void) {
  reset_all();

  /* Carve the arena into 2 windows: sender + receiver. */
  address_space_t windows[2];
  aspace_result_t ar = aspace_partition((uintptr_t)g_arena, ARENA_BYTES,
                                        windows, 2u);
  CHECK(ar == ASPACE_OK, "allow:partition_ok");

  cap_subject_id_t sender = 1u, receiver = 2u;

  /* Bind PCBs to those subjects, each with their own aspace. */
  process_id_t spid = 0u, rpid = 0u;
  CHECK(process_create(sender, &windows[0], &spid) == PROC_OK,
        "allow:process_create_sender");
  CHECK(process_create(receiver, &windows[1], &rpid) == PROC_OK,
        "allow:process_create_receiver");

  CHECK(cap_table_grant(sender, CAP_IPC_SEND) == CAP_OK,
        "allow:grant_send");
  CHECK(cap_table_grant(receiver, CAP_IPC_RECV) == CAP_OK,
        "allow:grant_recv");

  ipc_port_t port = IPC_PORT_INVALID;
  ipc_result_t pc = ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV,
                                    &port);
  CHECK(pc == IPC_OK && port != IPC_PORT_INVALID, "allow:port_create");

  /* Place the send envelope inside the sender's window. */
  ipc_msg_v0 *out_buf = (ipc_msg_v0 *)(uintptr_t)windows[0].base;
  fill_envelope(out_buf, 0x5Au);

  /* Recv buffer inside the receiver's window. */
  ipc_msg_v0 *in_buf = (ipc_msg_v0 *)(uintptr_t)windows[1].base;
  memset(in_buf, 0, sizeof(*in_buf));

  send_recv_ctx_t ctx = {sender, receiver, port, out_buf, in_buf,
                         IPC_OK, IPC_OK};
  do_send(&ctx);
  CHECK(ctx.send_rc == IPC_OK, "allow:send_ok");
  do_recv(&ctx);
  CHECK(ctx.recv_rc == IPC_OK, "allow:recv_ok");
  CHECK(in_buf->payload[0] == 0x5Au, "allow:payload_match");
  CHECK(in_buf->sender_subject == sender, "allow:sender_stamped");

  printf("TEST:PASS:ipc_bounds_allow\n");
}

/* --- Test 2: deny one-past-end --- */
static void test_deny_one_past_end(void) {
  reset_all();

  address_space_t windows[2];
  aspace_result_t ar = aspace_partition((uintptr_t)g_arena, ARENA_BYTES,
                                        windows, 2u);
  CHECK(ar == ASPACE_OK, "deny_past_end:partition_ok");

  cap_subject_id_t sender = 3u, receiver = 4u;
  process_id_t spid = 0u, rpid = 0u;
  CHECK(process_create(sender, &windows[0], &spid) == PROC_OK,
        "deny_past_end:process_create_sender");
  CHECK(process_create(receiver, &windows[1], &rpid) == PROC_OK,
        "deny_past_end:process_create_receiver");
  CHECK(cap_table_grant(sender, CAP_IPC_SEND) == CAP_OK,
        "deny_past_end:grant_send");
  CHECK(cap_table_grant(receiver, CAP_IPC_RECV) == CAP_OK,
        "deny_past_end:grant_recv");

  ipc_port_t port = IPC_PORT_INVALID;
  CHECK(ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) == IPC_OK,
        "deny_past_end:port_create");

  /* Stage an envelope OUTSIDE the sender's window — placed entirely
   * inside the receiver's window, which is past sender's [base,base+size). */
  ipc_msg_v0 *out_of_window =
      (ipc_msg_v0 *)(uintptr_t)(windows[0].base + windows[0].size);
  fill_envelope(out_of_window, 0x42u);

  /* Capture stdout to count the marker. */
  const char *tmp = "artifacts/tests/ipc_bounds_one_past_end_stdout.log";
  send_recv_ctx_t ctx = {sender, receiver, port, out_of_window, NULL,
                         IPC_OK, IPC_OK};
  CHECK(capture_stdout(tmp, do_send, &ctx) == 0,
        "deny_past_end:capture_ok");
  CHECK(ctx.send_rc == IPC_ERR_BOUNDS, "deny_past_end:send_rc");

  int n = count_marker_in_file(tmp, "CAP:DENY:3:ipc_send:bounds\n");
  CHECK(n == 1, "deny_past_end:marker_count_eq_1");

  printf("TEST:PASS:ipc_bounds_deny_one_past_end\n");
}

/* --- Test 3: straddle the upper boundary --- */
static void test_deny_straddle(void) {
  reset_all();

  address_space_t windows[2];
  aspace_result_t ar = aspace_partition((uintptr_t)g_arena, ARENA_BYTES,
                                        windows, 2u);
  CHECK(ar == ASPACE_OK, "deny_straddle:partition_ok");

  cap_subject_id_t sender = 5u, receiver = 6u;
  process_id_t spid = 0u, rpid = 0u;
  CHECK(process_create(sender, &windows[0], &spid) == PROC_OK,
        "deny_straddle:process_create_sender");
  CHECK(process_create(receiver, &windows[1], &rpid) == PROC_OK,
        "deny_straddle:process_create_receiver");
  CHECK(cap_table_grant(sender, CAP_IPC_SEND) == CAP_OK,
        "deny_straddle:grant_send");
  CHECK(cap_table_grant(receiver, CAP_IPC_RECV) == CAP_OK,
        "deny_straddle:grant_recv");

  ipc_port_t port = IPC_PORT_INVALID;
  CHECK(ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) == IPC_OK,
        "deny_straddle:port_create");

  /* Place the envelope at (base + size - 4): begins inside, ends past. */
  uintptr_t straddle_addr = windows[0].base + windows[0].size - 4u;
  ipc_msg_v0 *straddle_buf = (ipc_msg_v0 *)straddle_addr;
  fill_envelope(straddle_buf, 0x33u);

  const char *tmp = "artifacts/tests/ipc_bounds_straddle_stdout.log";
  send_recv_ctx_t ctx = {sender, receiver, port, straddle_buf, NULL,
                         IPC_OK, IPC_OK};
  CHECK(capture_stdout(tmp, do_send, &ctx) == 0,
        "deny_straddle:capture_ok");
  CHECK(ctx.send_rc == IPC_ERR_BOUNDS, "deny_straddle:send_rc");

  int n = count_marker_in_file(tmp, "CAP:DENY:5:ipc_send:bounds\n");
  CHECK(n == 1, "deny_straddle:marker_count_eq_1");

  printf("TEST:PASS:ipc_bounds_deny_straddle\n");
}

/* --- Test 4: backward-compat — no PCB means no enforcement --- */
static void test_no_pcb_skipped(void) {
  reset_all();

  /* NO process_create here — sender/receiver are raw cap_subject_id_t
   * values exactly the way ipc_sync_v0_test.c uses them. */
  cap_subject_id_t sender = 7u, receiver = 6u;
  CHECK(cap_table_grant(sender, CAP_IPC_SEND) == CAP_OK,
        "no_pcb:grant_send");
  CHECK(cap_table_grant(receiver, CAP_IPC_RECV) == CAP_OK,
        "no_pcb:grant_recv");

  ipc_port_t port = IPC_PORT_INVALID;
  CHECK(ipc_port_create(receiver, CAP_IPC_SEND, CAP_IPC_RECV, &port) == IPC_OK,
        "no_pcb:port_create");

  /* Use a stack buffer with no aspace context whatsoever. */
  ipc_msg_v0 out_buf;
  fill_envelope(&out_buf, 0x11u);
  ipc_msg_v0 in_buf;
  memset(&in_buf, 0, sizeof(in_buf));

  CHECK(ipc_send(sender, port, &out_buf) == IPC_OK,
        "no_pcb:send_ok_despite_no_aspace");
  CHECK(ipc_recv(receiver, port, &in_buf) == IPC_OK,
        "no_pcb:recv_ok_despite_no_aspace");
  CHECK(in_buf.payload[0] == 0x11u, "no_pcb:payload_match");

  printf("TEST:PASS:ipc_bounds_no_pcb_skipped\n");
}

int main(void) {
  /* Ensure the temp-capture directory exists. */
  (void)system("mkdir -p artifacts/tests");

  test_allow_in_window();
  test_deny_one_past_end();
  test_deny_straddle();
  test_no_pcb_skipped();

  if (g_fail_count != 0) {
    fprintf(stderr, "TEST:FAIL:ipc_bounds (failures=%d)\n", g_fail_count);
    return 1;
  }
  printf("TEST:PASS:ipc_bounds\n");
  return 0;
}
