/**
 * @file module_registry.c
 * @brief Implementation of the M1 acceptance-demo module registry
 *        (issue #251, plan #198 slice 4).
 *
 * See module_registry.h for the contract. This file owns the
 * compile-time module table and the three entry stubs that produce the
 * deterministic side-effects asserted by tests/m1_ipc_demo_test.c.
 *
 * Issue: #251.
 */

#include "module_registry.h"

#include <string.h>

#include "../cap/cap_handle.h"
#include "../cap/cap_table.h"
#include "../cap/capability.h"
#include "../ipc/ipc_msg.h"
#include "../ipc/ipc_ops.h"
#include "../ipc/ipc_port.h"
#include "process.h"
#include "proc_sched.h"
#include "../../user/include/secureos_abi.h"

/* Canonical demo payload. Pinned so tests can byte-compare. */
#define M1_DEMO_PAYLOAD "PING"
#define M1_DEMO_PAYLOAD_LEN 4u
#define M1_DEMO_TAG 0xC0DEu

/* --------------------------------------------------------------------
 * Transient demo state (port + per-subject handle map).
 * ------------------------------------------------------------------ */

#define M1_DEMO_MAX_SUBJECTS 8u

typedef struct {
  cap_subject_id_t subject;
  cap_handle_t     handle;
  bool             in_use;
} demo_handle_slot_t;

static ipc_port_t          g_demo_port = IPC_PORT_INVALID;
static demo_handle_slot_t  g_demo_handles[M1_DEMO_MAX_SUBJECTS];
static m1_demo_observations_t g_demo_obs;

static void demo_handle_record(cap_subject_id_t subject, cap_handle_t h) {
  /* Replace existing entry for the same subject if present. */
  for (uint32_t i = 0u; i < M1_DEMO_MAX_SUBJECTS; ++i) {
    if (g_demo_handles[i].in_use && g_demo_handles[i].subject == subject) {
      g_demo_handles[i].handle = h;
      return;
    }
  }
  for (uint32_t i = 0u; i < M1_DEMO_MAX_SUBJECTS; ++i) {
    if (!g_demo_handles[i].in_use) {
      g_demo_handles[i].in_use = true;
      g_demo_handles[i].subject = subject;
      g_demo_handles[i].handle = h;
      return;
    }
  }
  /* Silently drop — the demo never registers more than 3 subjects so
   * overflow indicates a test-harness bug. */
}

void m1_demo_reset(void) {
  g_demo_port = IPC_PORT_INVALID;
  memset(g_demo_handles, 0, sizeof(g_demo_handles));
  memset(&g_demo_obs, 0, sizeof(g_demo_obs));
}

bool m1_demo_set_port(ipc_port_t port) {
  if (port == IPC_PORT_INVALID) {
    return false;
  }
  g_demo_port = port;
  return true;
}

cap_handle_t m1_demo_get_handle_for(cap_subject_id_t subject) {
  for (uint32_t i = 0u; i < M1_DEMO_MAX_SUBJECTS; ++i) {
    if (g_demo_handles[i].in_use && g_demo_handles[i].subject == subject) {
      return g_demo_handles[i].handle;
    }
  }
  return CAP_HANDLE_NULL;
}

const m1_demo_observations_t *m1_demo_observations_for_tests(void) {
  return &g_demo_obs;
}

/* --------------------------------------------------------------------
 * Per-module entry stubs.
 *
 * Each stub runs inside a scheduled PCB (proc_sched_register sets up
 * the ucontext stack). They MUST NOT return — proc_exit() never
 * returns to its caller. The stubs record their observable behaviour
 * into g_demo_obs and emit the per-marker TEST:PASS lines that the
 * acceptance harness scrapes.
 * ------------------------------------------------------------------ */

static void entry_m1_receiver(void) {
  /* Look up our handle (issued at spawn time). */
  cap_handle_t recv_h = m1_demo_get_handle_for(SUBJECT_M1_RECEIVER);
  if (recv_h == CAP_HANDLE_NULL || g_demo_port == IPC_PORT_INVALID) {
    (void)proc_exit(901u);
  }

  ipc_msg_v0 in;
  memset(&in, 0xAA, sizeof(in));
  ipc_result_t r = ipc_recv_h(recv_h, g_demo_port, &in);
  if (r != IPC_OK) {
    (void)proc_exit(902u);
  }

  g_demo_obs.recv_ok++;
  g_demo_obs.recv_sender_subject = in.sender_subject;

  if (in.tag == M1_DEMO_TAG
      && in.payload_len == M1_DEMO_PAYLOAD_LEN
      && memcmp(in.payload, M1_DEMO_PAYLOAD, M1_DEMO_PAYLOAD_LEN) == 0
      && in.sender_subject == SUBJECT_M1_SENDER) {
    g_demo_obs.recv_payload_ok = 1u;
  }
  (void)proc_exit(0u);
}

static void demo_make_msg(ipc_msg_v0 *m) {
  memset(m, 0, sizeof(*m));
  m->abi_version = (uint16_t)OS_ABI_VERSION;
  m->flags = 0u;
  /* Deliberately set sender_subject to garbage so the test confirms
   * the handle-gated path overwrites it with the handle's owner. */
  m->sender_subject = 0xFEEDFACEu;
  m->tag = M1_DEMO_TAG;
  m->payload_len = M1_DEMO_PAYLOAD_LEN;
  memcpy(m->payload, M1_DEMO_PAYLOAD, M1_DEMO_PAYLOAD_LEN);
}

static void entry_m1_sender(void) {
  cap_handle_t send_h = m1_demo_get_handle_for(SUBJECT_M1_SENDER);
  if (send_h == CAP_HANDLE_NULL || g_demo_port == IPC_PORT_INVALID) {
    (void)proc_exit(801u);
  }

  ipc_msg_v0 m;
  demo_make_msg(&m);

  ipc_result_t r = ipc_send_h(send_h, g_demo_port, &m);
  if (r != IPC_OK) {
    (void)proc_exit(802u);
  }
  g_demo_obs.send_allow_ok = 1u;
  (void)proc_exit(0u);
}

static void entry_m1_unauth(void) {
  /* SUBJECT_M1_UNAUTH was spawned with a handle for the WRONG cap
   * (CAP_CONSOLE_WRITE — see g_modules below). The handle resolves
   * with owner == SUBJECT_M1_UNAUTH, but its cap_id mismatches the
   * port's required CAP_IPC_SEND, so cap_gate_check_handle_result
   * returns CAP_ERR_MISSING and ipc_send_h emits the canonical
   *   CAP:DENY:<SUBJECT_M1_UNAUTH>:ipc_send:-
   * marker (subject id is the kernel-trusted handle owner, so the
   * marker carries 259 — SUBJECT_M1_UNAUTH — not 0).
   *
   * The test also asserts the receiver remains BLOCKED when the unauth
   * module runs in isolation (no sender, no other waker). */
  if (g_demo_port == IPC_PORT_INVALID) {
    (void)proc_exit(701u);
  }
  cap_handle_t bad_h = m1_demo_get_handle_for(SUBJECT_M1_UNAUTH);
  if (bad_h == CAP_HANDLE_NULL) {
    (void)proc_exit(702u);
  }

  ipc_msg_v0 m;
  demo_make_msg(&m);

  ipc_result_t r = ipc_send_h(bad_h, g_demo_port, &m);
  if (r == IPC_ERR_CAP_DENIED) {
    g_demo_obs.send_deny_cap_denied = 1u;
  }
  (void)proc_exit(0u);
}

/* --------------------------------------------------------------------
 * Module registry table.
 * ------------------------------------------------------------------ */

typedef struct {
  const char       *name;
  cap_subject_id_t  subject;
  proc_entry_fn_t   entry;
  /* `declared_cap == 0` means "no cap granted at spawn time". The
   * capability_id_t enum starts at 1 so 0 is a safe sentinel. */
  capability_id_t   declared_cap;
} module_descriptor_t;

static const module_descriptor_t g_modules[] = {
  { "m1-sender",   SUBJECT_M1_SENDER,   entry_m1_sender,   CAP_IPC_SEND },
  { "m1-receiver", SUBJECT_M1_RECEIVER, entry_m1_receiver, CAP_IPC_RECV },
  /* m1-unauth gets a deliberately wrong cap (CAP_CONSOLE_WRITE) so the
   * deny marker emitted by ipc_send_h carries SUBJECT_M1_UNAUTH as the
   * authenticated handle owner — matching the plan's expectation of
   * a deny marker scoped to m1-unauth's identity, not subject 0. */
  { "m1-unauth",   SUBJECT_M1_UNAUTH,   entry_m1_unauth,   CAP_CONSOLE_WRITE },
};

#define M1_MODULE_COUNT (sizeof(g_modules) / sizeof(g_modules[0]))

static const module_descriptor_t *lookup_module(const char *name) {
  if (name == NULL) {
    return NULL;
  }
  for (uint32_t i = 0u; i < M1_MODULE_COUNT; ++i) {
    if (strcmp(g_modules[i].name, name) == 0) {
      return &g_modules[i];
    }
  }
  return NULL;
}

module_spawn_result_t proc_spawn_module(const char *name,
                                        process_id_t *out_pid) {
  if (out_pid == NULL) {
    return MODULE_SPAWN_ERR_INVALID_ARG;
  }
  const module_descriptor_t *m = lookup_module(name);
  if (m == NULL) {
    return MODULE_SPAWN_ERR_UNKNOWN_NAME;
  }

  process_id_t pid = PID_INVALID;
  if (process_create(m->subject, NULL, &pid) != PROC_OK) {
    return MODULE_SPAWN_ERR_PROC_CREATE;
  }

  if (m->declared_cap != (capability_id_t)0) {
    /* Grant via the legacy bitset so the cap_check audit-ring path in
     * ipc_send_h / ipc_recv_h sees a matching grant (ipc_ops.c calls
     * cap_check for audit parity on both allow and deny paths). */
    if (cap_table_grant(m->subject, m->declared_cap) != CAP_OK) {
      return MODULE_SPAWN_ERR_CAP_GRANT;
    }
    /* Authoritative handle for the handle-gated decision. */
    cap_handle_t h = cap_handle_grant(m->subject, m->declared_cap);
    if (h == CAP_HANDLE_NULL) {
      return MODULE_SPAWN_ERR_CAP_GRANT;
    }
    demo_handle_record(m->subject, h);
  }

  if (proc_sched_register(pid, m->entry) != PROC_SCHED_OK) {
    return MODULE_SPAWN_ERR_SCHED_REGISTER;
  }

  *out_pid = pid;
  return MODULE_SPAWN_OK;
}
