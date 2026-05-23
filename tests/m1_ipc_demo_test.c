/**
 * @file m1_ipc_demo_test.c
 * @brief Acceptance harness for the M1 two-module IPC demo
 *        (issue #251, plan #198 slice 4, BUILD_ROADMAP §5.1).
 *
 * Covers the two §5.1 acceptance bullets:
 *
 *   1. "two modules exchange message" — m1-sender → m1-receiver
 *      round-trip via ipc_send_h / ipc_recv_h across two PCBs.
 *      The kernel-stamped sender_subject reaches the receiver as
 *      SUBJECT_M1_SENDER (not the attacker-supplied 0xFEEDFACE).
 *      Emits TEST:PASS:m1_ipc_allow.
 *
 *   2. "unauthorized operation denied with explicit error" —
 *      m1-unauth attempts ipc_send_h with a wrong-cap handle
 *      (CAP_CONSOLE_WRITE) and is denied. ipc_send_h returns
 *      IPC_ERR_CAP_DENIED, exactly one
 *      CAP:DENY:<SUBJECT_M1_UNAUTH>:ipc_send:- marker is emitted,
 *      and the receiver remains BLOCKED with no envelope delivered.
 *      Emits TEST:PASS:m1_ipc_deny.
 *
 *   Aggregate marker TEST:PASS:m1_ipc is the harness rollup.
 *
 * Launched by:
 *   build/scripts/test_m1_ipc_demo.sh (dispatched via test.sh and
 *   validate_bundle.sh).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/module_registry.h"
#include "../kernel/proc/proc_sched.h"
#include "../kernel/proc/process.h"

static void die(const char *reason) {
  printf("TEST:FAIL:m1_ipc:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  m1_demo_reset();
  proc_sched_reset();
  process_table_reset();
  ipc_port_table_reset();
  cap_reset_for_tests();
  cap_table_reset();
  cap_handle_table_reset();
  cap_audit_reset_for_tests();
}

/* ------------------------------------------------------------------ */
/* (1) Allow path                                                      */
/* ------------------------------------------------------------------ */

static void test_allow(void) {
  reset_world();

  /* Receiver must hold CAP_IPC_RECV before the port is created — the
   * scheduler hasn't run yet so the spawn-time grant ordering matters.
   * Spawn the receiver first so its handle is recorded before we make
   * the port (the port's owner field references SUBJECT_M1_RECEIVER). */
  process_id_t pr = PID_INVALID;
  if (proc_spawn_module("m1-receiver", &pr) != MODULE_SPAWN_OK) {
    die("spawn_receiver");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(SUBJECT_M1_RECEIVER, CAP_IPC_SEND, CAP_IPC_RECV, &port)
        != IPC_OK
      || port == IPC_PORT_INVALID) {
    die("port_create");
  }
  if (!m1_demo_set_port(port)) {
    die("set_port");
  }

  process_id_t ps = PID_INVALID;
  if (proc_spawn_module("m1-sender", &ps) != MODULE_SPAWN_OK) {
    die("spawn_sender");
  }

  if (proc_sched_run() != PROC_SCHED_OK) {
    die("sched_run_allow");
  }

  const m1_demo_observations_t *obs = m1_demo_observations_for_tests();
  if (!obs->send_allow_ok)       die("send_did_not_succeed");
  if (!obs->recv_ok)             die("recv_did_not_complete");
  if (!obs->recv_payload_ok)     die("recv_payload_mismatch");
  if (obs->recv_sender_subject != SUBJECT_M1_SENDER) {
    die("sender_subject_not_kernel_stamped");
  }

  process_t snap;
  if (process_lookup(pr, &snap) != PROC_OK
      || snap.state != PROC_STATE_EXITED
      || snap.exit_code != 0u) {
    die("receiver_bad_terminal");
  }
  if (process_lookup(ps, &snap) != PROC_OK
      || snap.state != PROC_STATE_EXITED
      || snap.exit_code != 0u) {
    die("sender_bad_terminal");
  }

  printf("TEST:PASS:m1_ipc_allow\n");
}

/* ------------------------------------------------------------------ */
/* (2) Deny path                                                       */
/* ------------------------------------------------------------------ */

/* We need to capture the CAP:DENY: line emitted by ipc_send_h on the
 * deny path so the test can pin: (a) marker present, (b) marker
 * appears exactly once. We re-route stdout through a pipe for the
 * duration of the scheduler run, then drain it and re-emit so the
 * outer log keeps the line visible to the test_*.sh grep.
 */
#include <fcntl.h>
#include <unistd.h>

static char g_deny_capture[8192];
static size_t g_deny_capture_len = 0u;

static int dup_stdout_fd = -1;
static int pipe_fds[2] = {-1, -1};

static void capture_begin(void) {
  fflush(stdout);
  if (pipe(pipe_fds) != 0) die("pipe");
  /* Make the read end non-blocking so capture_end's drain loop
   * terminates after the writer is restored. */
  int flags = fcntl(pipe_fds[0], F_GETFL, 0);
  (void)fcntl(pipe_fds[0], F_SETFL, flags | O_NONBLOCK);

  dup_stdout_fd = dup(STDOUT_FILENO);
  if (dup_stdout_fd < 0) die("dup_stdout");
  if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) die("dup2_pipe");
  close(pipe_fds[1]);
  pipe_fds[1] = -1;
  g_deny_capture_len = 0u;
  g_deny_capture[0] = '\0';
}

static void capture_end(void) {
  fflush(stdout);
  /* Restore stdout BEFORE draining so any newly-printed bytes go to
   * the real stdout, not the pipe. */
  if (dup2(dup_stdout_fd, STDOUT_FILENO) < 0) die("dup2_restore");
  close(dup_stdout_fd);
  dup_stdout_fd = -1;

  for (;;) {
    char buf[1024];
    ssize_t n = read(pipe_fds[0], buf, sizeof(buf));
    if (n <= 0) break;
    size_t room = sizeof(g_deny_capture) - 1u - g_deny_capture_len;
    size_t take = ((size_t)n < room) ? (size_t)n : room;
    memcpy(g_deny_capture + g_deny_capture_len, buf, take);
    g_deny_capture_len += take;
    g_deny_capture[g_deny_capture_len] = '\0';
  }
  close(pipe_fds[0]);
  pipe_fds[0] = -1;

  /* Re-emit captured bytes so the outer log retains visibility. */
  fwrite(g_deny_capture, 1, g_deny_capture_len, stdout);
  fflush(stdout);
}

static size_t count_substr(const char *hay, const char *needle) {
  size_t n = 0u;
  size_t nl = strlen(needle);
  if (nl == 0u) return 0u;
  const char *p = hay;
  while ((p = strstr(p, needle)) != NULL) {
    n++;
    p += nl;
  }
  return n;
}

static void test_deny(void) {
  reset_world();

  /* Receiver first so it blocks on the port and we can observe it. */
  process_id_t pr = PID_INVALID;
  if (proc_spawn_module("m1-receiver", &pr) != MODULE_SPAWN_OK) {
    die("spawn_receiver_deny");
  }

  ipc_port_t port = IPC_PORT_INVALID;
  if (ipc_port_create(SUBJECT_M1_RECEIVER, CAP_IPC_SEND, CAP_IPC_RECV, &port)
        != IPC_OK
      || port == IPC_PORT_INVALID) {
    die("port_create_deny");
  }
  if (!m1_demo_set_port(port)) die("set_port_deny");

  process_id_t pu = PID_INVALID;
  if (proc_spawn_module("m1-unauth", &pu) != MODULE_SPAWN_OK) {
    die("spawn_unauth");
  }

  capture_begin();
  proc_sched_result_t rr = proc_sched_run();
  capture_end();

  /* Receiver remains blocked (no envelope), unauth exits cleanly, so
   * the scheduler must classify this as a deadlock (one PCB BLOCKED,
   * no possible waker). */
  if (rr != PROC_SCHED_ERR_DEADLOCK) {
    die("deny_did_not_deadlock");
  }

  const m1_demo_observations_t *obs = m1_demo_observations_for_tests();
  if (!obs->send_deny_cap_denied) die("unauth_send_not_denied");
  if (obs->recv_ok)               die("recv_completed_on_deny");
  if (obs->send_allow_ok)         die("allow_path_ran_on_deny");

  /* Deny marker shape per docs/abi/capability-deny-contract.md §4 and
   * ipc_ops.c's ipc_emit_deny_marker: "CAP:DENY:<subject>:ipc_send:-".
   */
  char expected[64];
  snprintf(expected, sizeof(expected),
           "CAP:DENY:%u:ipc_send:-\n", (unsigned)SUBJECT_M1_UNAUTH);
  size_t hits = count_substr(g_deny_capture, expected);
  if (hits == 0u) die("deny_marker_missing");
  if (hits > 1u)  die("deny_marker_emitted_more_than_once");

  /* Envelope must not have leaked into the port slot. */
  if (ipc_port_has_pending_for_tests(port)) {
    die("envelope_leaked_on_deny");
  }

  /* Receiver PCB must still be BLOCKED on the port (never woken). */
  process_t snap;
  if (process_lookup(pr, &snap) != PROC_OK) die("lookup_receiver");
  if (snap.state != PROC_STATE_BLOCKED) die("receiver_not_blocked_after_deny");

  /* Unauth PCB exited cleanly with exit_code 0 (the entry returns
   * proc_exit(0) on the denied-as-expected branch). */
  if (process_lookup(pu, &snap) != PROC_OK
      || snap.state != PROC_STATE_EXITED
      || snap.exit_code != 0u) {
    die("unauth_bad_terminal");
  }

  printf("TEST:PASS:m1_ipc_deny\n");
}

int main(void) {
  test_allow();
  test_deny();
  printf("TEST:PASS:m1_ipc\n");
  return 0;
}
