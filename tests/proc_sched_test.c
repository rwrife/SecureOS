/**
 * @file proc_sched_test.c
 * @brief Acceptance harness for the M1 cooperative scheduler + IPC
 *        block/wake hooks (issue #250, plan #198 slice 3).
 *
 * Asserts:
 *   1. proc_sched_register transitions a NEW PCB to READY and the
 *      scheduler dispatches it exactly once on proc_sched_run.
 *   2. Two cooperatively-yielding PCBs interleave round-robin and
 *      both reach EXITED with the recorded exit_code.
 *   3. ipc_recv on an empty port blocks the receiver; a peer's
 *      ipc_send wakes exactly that receiver and the rendezvous
 *      completes with the kernel-stamped sender_subject.
 *   4. ipc_send on a full single-waiter slot blocks the sender; the
 *      receiver's ipc_recv consumes the staged envelope and wakes the
 *      sender, which then re-stages successfully.
 *   5. Deadlock detection: a single PCB blocked on a port nobody will
 *      ever send to causes proc_sched_run to return
 *      PROC_SCHED_ERR_DEADLOCK without hanging.
 *
 * Launched by:
 *   build/scripts/test_proc_sched.sh (dispatched via
 *   build/scripts/test.sh proc_sched).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/address_space.h"
#include "../kernel/proc/proc_sched.h"
#include "../kernel/proc/process.h"
#include "../user/include/secureos_abi.h"

static void die(const char *reason) {
  printf("TEST:FAIL:proc_sched:%s\n", reason);
  exit(1);
}

/* Real partitioned address-space arena (issue #260 done-when 3).
 *
 * The scheduler now asserts `aspace_invariant_ok(pcb->aspace)` before
 * restoring any PCB's context. A sentinel pointer like
 * `(address_space_t *)0x1000` fails that check immediately (it has no
 * valid `base`/`size`/`stack_top`), so this test backs every PCB with
 * a real partitioned slot carved out of a host-side arena. The arena
 * lives in BSS so its address is stable across tests; the partition
 * is rebuilt by `reset_world()` so each test gets identical inputs.
 *
 * Arena sized for PROC_TABLE_MAX windows so any test can claim up to
 * the table maximum without exhausting the partition.
 */
static uint8_t g_test_arena[PROC_TABLE_MAX *
                            (PROC_KSTACK_BYTES + IPC_MSG_PAYLOAD_MAX + 64u)];
static address_space_t g_test_aspaces[PROC_TABLE_MAX];
static size_t g_test_aspace_next = 0u;

static void reset_world(void) {
  proc_sched_reset();
  process_table_reset();
  ipc_port_table_reset();
  cap_reset_for_tests();
  cap_table_reset();

  /* Rebuild the test partition every reset so each case sees a fresh,
   * well-formed arena. PROC_TABLE_MAX slots is more than any single
   * test consumes. */
  aspace_result_t pr = aspace_partition(
      (uintptr_t)g_test_arena, sizeof(g_test_arena),
      g_test_aspaces, PROC_TABLE_MAX);
  if (pr != ASPACE_OK) {
    printf("TEST:FAIL:proc_sched:arena_partition_failed:%d\n", (int)pr);
    exit(1);
  }
  g_test_aspace_next = 0u;
}

/* Claim the next partitioned address-space slot. Each test PCB gets
 * its own slot — the original `fake_aspace(tag)` sentinels are
 * preserved as comments so the diff against #260 stays readable. */
static address_space_t *next_aspace(void) {
  if (g_test_aspace_next >= PROC_TABLE_MAX) {
    printf("TEST:FAIL:proc_sched:aspace_arena_exhausted\n");
    exit(1);
  }
  return &g_test_aspaces[g_test_aspace_next++];
}

/* ------------------------------------------------------------------ */
/* (1) Single-PCB dispatch + exit_code                                  */
/* ------------------------------------------------------------------ */

static int g_single_ticks = 0;
static void entry_single(void) {
  g_single_ticks++;
  (void)proc_exit(42u);
  /* Should never return. */
  g_single_ticks = -1000;
}

static void test_single_dispatch(void) {
  reset_world();
  cap_subject_id_t subj = 1u;
  cap_table_grant(subj, CAP_IPC_SEND);
  cap_table_grant(subj, CAP_IPC_RECV);

  process_id_t pid = PID_INVALID;
  if (process_create(subj, next_aspace(), &pid) != PROC_OK) die("create_single");
  if (proc_sched_register(pid, entry_single) != PROC_SCHED_OK) die("register_single");

  process_t snap;
  if (process_lookup(pid, &snap) != PROC_OK) die("lookup_after_register");
  if (snap.state != PROC_STATE_READY) die("state_after_register_not_ready");

  if (proc_sched_run() != PROC_SCHED_OK) die("run_single");

  if (g_single_ticks != 1) die("entry_not_called_once");
  if (process_lookup(pid, &snap) != PROC_OK) die("lookup_after_run");
  if (snap.state != PROC_STATE_EXITED) die("state_after_run_not_exited");
  if (snap.exit_code != 42u) die("exit_code_not_recorded");

  printf("TEST:PASS:proc_sched_single_dispatch\n");
}

/* ------------------------------------------------------------------ */
/* (2) Round-robin yield between two PCBs                              */
/* ------------------------------------------------------------------ */

static int g_trace[16];
static int g_trace_len = 0;
static void trace(int marker) {
  if (g_trace_len < (int)(sizeof(g_trace) / sizeof(g_trace[0]))) {
    g_trace[g_trace_len++] = marker;
  }
}

static void entry_a(void) {
  trace(1);
  (void)proc_yield();
  trace(3);
  (void)proc_yield();
  trace(5);
  (void)proc_exit(0u);
}
static void entry_b(void) {
  trace(2);
  (void)proc_yield();
  trace(4);
  (void)proc_exit(0u);
}

static void test_round_robin(void) {
  reset_world();
  cap_subject_id_t sa = 2u, sb = 3u;
  process_id_t pa = PID_INVALID, pb = PID_INVALID;
  if (process_create(sa, next_aspace(), &pa) != PROC_OK) die("create_a");
  if (process_create(sb, next_aspace(), &pb) != PROC_OK) die("create_b");
  if (proc_sched_register(pa, entry_a) != PROC_SCHED_OK) die("register_a");
  if (proc_sched_register(pb, entry_b) != PROC_SCHED_OK) die("register_b");

  g_trace_len = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_round_robin");

  /* Expected interleave: A1 B2 A3 B4 A5. */
  static const int expected[] = {1, 2, 3, 4, 5};
  if (g_trace_len != 5) die("trace_len_mismatch");
  for (int i = 0; i < 5; ++i) {
    if (g_trace[i] != expected[i]) die("round_robin_interleave_mismatch");
  }

  process_t s;
  if (process_lookup(pa, &s) != PROC_OK || s.state != PROC_STATE_EXITED) die("a_not_exited");
  if (process_lookup(pb, &s) != PROC_OK || s.state != PROC_STATE_EXITED) die("b_not_exited");

  printf("TEST:PASS:proc_sched_round_robin\n");
}

/* ------------------------------------------------------------------ */
/* (3) recv-blocks-until-send                                          */
/* ------------------------------------------------------------------ */

static ipc_port_t g_recv_port = 0u;
static cap_subject_id_t g_recv_subj = 0u;
static cap_subject_id_t g_send_subj = 0u;
static int g_recv_ok = 0;
static int g_recv_blocked_observed = 0;
static int g_send_after_recv_block = 0;

static void entry_receiver(void) {
  ipc_msg_v0 in;
  memset(&in, 0, sizeof(in));
  ipc_result_t r = ipc_recv(g_recv_subj, g_recv_port, &in);
  if (r != IPC_OK) {
    (void)proc_exit(101u);
  }
  if (in.sender_subject != g_send_subj) {
    (void)proc_exit(102u);
  }
  if (in.payload_len != 4u || memcmp(in.payload, "PING", 4) != 0) {
    (void)proc_exit(103u);
  }
  g_recv_ok = 1;
  (void)proc_exit(0u);
}

static void entry_sender(void) {
  /* Observe that the receiver is BLOCKED before we send. */
  if (proc_sched_blocked_count_for_tests() == 1u) {
    g_recv_blocked_observed = 1;
  }
  ipc_msg_v0 m;
  memset(&m, 0, sizeof(m));
  m.abi_version = (uint16_t)OS_ABI_VERSION;
  m.tag = 0u;
  m.payload_len = 4u;
  memcpy(m.payload, "PING", 4);
  ipc_result_t r = ipc_send(g_send_subj, g_recv_port, &m);
  if (r != IPC_OK) {
    (void)proc_exit(201u);
  }
  g_send_after_recv_block = 1;
  (void)proc_exit(0u);
}

static void test_recv_blocks_until_send(void) {
  reset_world();
  g_recv_subj = 1u;
  g_send_subj = 2u;
  cap_table_grant(g_send_subj, CAP_IPC_SEND);
  cap_table_grant(g_recv_subj, CAP_IPC_RECV);

  if (ipc_port_create(g_recv_subj, CAP_IPC_SEND, CAP_IPC_RECV, &g_recv_port) != IPC_OK) {
    die("port_create");
  }

  process_id_t pr = PID_INVALID, ps = PID_INVALID;
  if (process_create(g_recv_subj, next_aspace(), &pr) != PROC_OK) die("create_recv");
  if (process_create(g_send_subj, next_aspace(), &ps) != PROC_OK) die("create_send");
  /* Register receiver first so it runs first and is the one that blocks. */
  if (proc_sched_register(pr, entry_receiver) != PROC_SCHED_OK) die("register_recv");
  if (proc_sched_register(ps, entry_sender) != PROC_SCHED_OK) die("register_send");

  g_recv_ok = 0;
  g_recv_blocked_observed = 0;
  g_send_after_recv_block = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_recv_blocks");

  if (!g_recv_blocked_observed) die("sender_did_not_see_receiver_blocked");
  if (!g_send_after_recv_block) die("send_did_not_complete");
  if (!g_recv_ok) die("recv_did_not_unblock_with_payload");

  process_t s;
  if (process_lookup(pr, &s) != PROC_OK || s.state != PROC_STATE_EXITED || s.exit_code != 0u) {
    die("recv_pcb_bad_terminal");
  }
  if (process_lookup(ps, &s) != PROC_OK || s.state != PROC_STATE_EXITED || s.exit_code != 0u) {
    die("send_pcb_bad_terminal");
  }

  printf("TEST:PASS:proc_sched_recv_blocks_until_send\n");
}

/* ------------------------------------------------------------------ */
/* (4) send-blocks-on-full-slot                                        */
/* ------------------------------------------------------------------ */

static ipc_port_t g_full_port = 0u;
static cap_subject_id_t g_owner_subj = 0u;
static cap_subject_id_t g_blocker_subj = 0u;
static int g_second_send_unblocked = 0;
static int g_owner_drained = 0;

static void entry_blocker_send(void) {
  /* First send fills the slot; second send must block until the owner
   * drains. */
  ipc_msg_v0 m;
  memset(&m, 0, sizeof(m));
  m.abi_version = (uint16_t)OS_ABI_VERSION;
  m.payload_len = 1u;
  m.payload[0] = 'A';
  if (ipc_send(g_blocker_subj, g_full_port, &m) != IPC_OK) (void)proc_exit(301u);
  m.payload[0] = 'B';
  /* Slot is full; without a scheduler this would return PEER_GONE.
   * With the scheduler this must block until owner consumes 'A'. */
  if (ipc_send(g_blocker_subj, g_full_port, &m) != IPC_OK) (void)proc_exit(302u);
  g_second_send_unblocked = 1;
  (void)proc_exit(0u);
}

static void entry_owner_drain(void) {
  /* Yield so the sender runs first and stages + blocks. */
  (void)proc_yield();
  ipc_msg_v0 in;
  memset(&in, 0, sizeof(in));
  if (ipc_recv(g_owner_subj, g_full_port, &in) != IPC_OK) (void)proc_exit(401u);
  if (in.payload_len != 1u || in.payload[0] != 'A') (void)proc_exit(402u);
  /* Drain the second envelope after the sender re-stages. */
  if (ipc_recv(g_owner_subj, g_full_port, &in) != IPC_OK) (void)proc_exit(403u);
  if (in.payload_len != 1u || in.payload[0] != 'B') (void)proc_exit(404u);
  g_owner_drained = 1;
  (void)proc_exit(0u);
}

static void test_send_blocks_on_full(void) {
  reset_world();
  g_owner_subj = 1u;
  g_blocker_subj = 2u;
  cap_table_grant(g_owner_subj, CAP_IPC_RECV);
  cap_table_grant(g_blocker_subj, CAP_IPC_SEND);

  if (ipc_port_create(g_owner_subj, CAP_IPC_SEND, CAP_IPC_RECV, &g_full_port) != IPC_OK) {
    die("full_port_create");
  }

  process_id_t po = PID_INVALID, pb = PID_INVALID;
  if (process_create(g_blocker_subj, next_aspace(), &pb) != PROC_OK) die("create_blocker");
  if (process_create(g_owner_subj, next_aspace(), &po) != PROC_OK) die("create_owner");
  /* Register sender first so it runs and fills the slot before the owner. */
  if (proc_sched_register(pb, entry_blocker_send) != PROC_SCHED_OK) die("register_blocker");
  if (proc_sched_register(po, entry_owner_drain) != PROC_SCHED_OK) die("register_owner");

  g_second_send_unblocked = 0;
  g_owner_drained = 0;
  if (proc_sched_run() != PROC_SCHED_OK) die("run_send_blocks_on_full");

  if (!g_second_send_unblocked) die("sender_did_not_unblock");
  if (!g_owner_drained) die("owner_did_not_drain");

  printf("TEST:PASS:proc_sched_send_blocks_on_full\n");
}

/* ------------------------------------------------------------------ */
/* (5) Deadlock detection                                              */
/* ------------------------------------------------------------------ */

static ipc_port_t g_dead_port = 0u;
static cap_subject_id_t g_dead_subj = 0u;
static int g_deadlock_recv_returned = 0;

static void entry_dead_recv(void) {
  ipc_msg_v0 in;
  memset(&in, 0, sizeof(in));
  /* No peer will ever send. The scheduler must return DEADLOCK rather
   * than hang. The recv call will not return in this test. */
  (void)ipc_recv(g_dead_subj, g_dead_port, &in);
  g_deadlock_recv_returned = 1;
  (void)proc_exit(0u);
}

static void test_deadlock_detection(void) {
  reset_world();
  g_dead_subj = 1u;
  cap_table_grant(g_dead_subj, CAP_IPC_RECV);

  if (ipc_port_create(g_dead_subj, CAP_IPC_SEND, CAP_IPC_RECV, &g_dead_port) != IPC_OK) {
    die("dead_port_create");
  }
  process_id_t pd = PID_INVALID;
  if (process_create(g_dead_subj, next_aspace(), &pd) != PROC_OK) die("create_dead");
  if (proc_sched_register(pd, entry_dead_recv) != PROC_SCHED_OK) die("register_dead");

  g_deadlock_recv_returned = 0;
  proc_sched_result_t r = proc_sched_run();
  if (r != PROC_SCHED_ERR_DEADLOCK) die("expected_deadlock");
  if (g_deadlock_recv_returned) die("recv_should_not_have_returned");

  process_t s;
  if (process_lookup(pd, &s) != PROC_OK) die("lookup_after_deadlock");
  if (s.state != PROC_STATE_BLOCKED) die("expected_blocked_state");

  printf("TEST:PASS:proc_sched_deadlock_detected\n");
}

/* ------------------------------------------------------------------ */

int main(void) {
  printf("TEST:START:proc_sched\n");
  test_single_dispatch();
  test_round_robin();
  test_recv_blocks_until_send();
  test_send_blocks_on_full();
  test_deadlock_detection();
  printf("TEST:PASS:proc_sched\n");
  return 0;
}
