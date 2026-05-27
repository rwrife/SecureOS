/**
 * @file broker_svc.c
 * @brief M4-on-M1 capability-broker service implementation — slice 1.
 *
 * See `broker_svc.h` for the public contract and
 * `plans/2026-05-25-m4-broker-on-m1-substrate.md` §"Broker service
 * module" for the design context.
 *
 * Slice scope (intentionally narrow — issue #302):
 *   - Allocate one well-known port at init.
 *   - Expose the handle for downstream slices (#303, #304, #305).
 *   - Do NOT spin a recv loop or dispatch broker request/approve/
 *     deny/revoke envelopes yet — those land with the acceptance
 *     peers (#304 / #305), together with the `BROKER_OP_*` tag enum
 *     and the bounded `share_id -> cap_handle_t` side-table.
 *
 * Boot-order edge:
 *   `broker_svc_init()` MUST run AFTER `ipc_port_table_init()` (it
 *   calls `ipc_port_create`) and AFTER `console_svc_init()` /
 *   `fs_svc_init()` so the well-known port indexes stay deterministic
 *   across boots (console = slot 1, fs read = slot 2, fs write = slot
 *   3, broker = slot 4 in a fresh port table). It must run BEFORE any
 *   module-registry walk that may try to mint a handle for the
 *   broker-svc port. In host tests, the test driver controls ordering
 *   directly.
 *
 * Issue: #302. Plan: plans/2026-05-25-m4-broker-on-m1-substrate.md
 * slice 1.
 */

#include "broker_svc.h"

#include <stdint.h>

/* broker_svc.c is compiled into the freestanding kernel
 * (see build/scripts/build_kernel_entry.sh, --target=x86_64-unknown-none-elf
 * -ffreestanding). <stdio.h> is only available in hosted host-test builds;
 * gate it (and the stdout fwrite path) accordingly. Sibling deny-marker
 * emitters (kernel/proc/process.c, kernel/ipc/ipc_ops.c) avoid this trap
 * by being host-only — broker_svc.c is not, so the marker emit path is
 * a host-only courtesy here. The marker is still asserted by host fixtures
 * which exercise this code via the same translation unit. */
#if __STDC_HOSTED__
#include <stdio.h>
#endif

#include "../../tests/harness/svc_subjects.h"
#include "../cap/cap_broker.h"
#include "../cap/cap_deny_marker.h"
#include "../cap/cap_handle.h"
#include "../cap/capability.h"
#include "../ipc/ipc_port.h"
#include "../proc/process.h"

/*
 * Module-private state. A single port handle plus an "initialised"
 * latch. No dynamic allocation; no other state — the recv loop, the
 * op-dispatch step, and the `share_id -> cap_handle_t` side-table
 * land in slice 3/4.
 */
static ipc_port_t g_broker_svc_port = IPC_PORT_INVALID;
static bool g_broker_svc_initialised = false;

/*
 * M5-SUBSTRATE-002 (#324) side-table. One row per minted recipient
 * handle. The table is sized to mirror `CAP_BROKER_MAX_SHARES`
 * (kernel/cap/cap_broker.c) so a 1:1 mapping is always possible.
 * No heap; deterministic walk in slot order.
 *
 * `child_handle == CAP_HANDLE_NULL` marks an empty slot.
 */
typedef struct {
  cap_share_id_t   share_id;
  cap_subject_id_t owner_subject;
  cap_subject_id_t recipient_subject;
  cap_handle_t     child_handle;
} broker_svc_share_row_t;

static broker_svc_share_row_t g_broker_shares[CAP_BROKER_MAX_SHARES];

static void broker_svc_shares_clear(void) {
  for (size_t i = 0u; i < CAP_BROKER_MAX_SHARES; ++i) {
    g_broker_shares[i].share_id          = CAP_SHARE_ID_INVALID;
    g_broker_shares[i].owner_subject     = 0u;
    g_broker_shares[i].recipient_subject = 0u;
    g_broker_shares[i].child_handle      = CAP_HANDLE_NULL;
  }
}

broker_svc_result_t broker_svc_init(void) {
  if (g_broker_svc_initialised) {
    return BROKER_SVC_ERR_ALREADY_INIT;
  }

  ipc_port_t handle = IPC_PORT_INVALID;
  ipc_result_t rc = ipc_port_create((cap_subject_id_t)SUBJECT_M4_BROKER_SVC,
                                    CAP_IPC_SEND,
                                    CAP_IPC_SEND,
                                    &handle);
  if (rc != IPC_OK || handle == IPC_PORT_INVALID) {
    return BROKER_SVC_ERR_PORT_ALLOC;
  }

  g_broker_svc_port = handle;
  g_broker_svc_initialised = true;
  broker_svc_shares_clear();
  return BROKER_SVC_OK;
}

void broker_svc_reset(void) {
  /* Intentionally do not call ipc_port_destroy() here. Tests that want
   * the port released use ipc_port_table_reset() in the same phase;
   * kernel-side reset is owned by the boot sequence in a future slice
   * if needed. Same convention as console_svc_reset() /
   * fs_svc_reset(). */
  g_broker_svc_port = IPC_PORT_INVALID;
  g_broker_svc_initialised = false;
  broker_svc_shares_clear();
}

ipc_port_t broker_svc_port(void) {
  return g_broker_svc_port;
}

bool broker_svc_is_initialised(void) {
  return g_broker_svc_initialised;
}

/* ----------------------------------------------------------------
 * M5-SUBSTRATE-002 (#324): broker_svc_approve + delete-owner cascade.
 *
 * Gated on __STDC_HOSTED__: these functions reference cap_broker_*,
 * cap_handle_*, cap_deny_marker_format and process_destroy, which are
 * NOT in build/scripts/build_kernel_entry.sh's freestanding kernel
 * link list (only host-side test_*.sh scripts pull those .c files
 * into the link). Including them in the freestanding compile would
 * break the kernel-entry build with undefined-symbol errors at
 * link time. Host fixtures continue to exercise this code via the
 * same translation unit.
 *
 * Lifting the gate is a separate follow-up that adds the supporting
 * .c files (cap_broker.c, cap_handle.c, cap_deny_marker.c,
 * proc/process.c) to the kernel link list — see issue #324
 * "Kernel integration" follow-up notes.
 * ---------------------------------------------------------------- */

#if __STDC_HOSTED__

static broker_svc_share_row_t *broker_svc_find_free_row(void) {
  for (size_t i = 0u; i < CAP_BROKER_MAX_SHARES; ++i) {
    if (g_broker_shares[i].child_handle == CAP_HANDLE_NULL) {
      return &g_broker_shares[i];
    }
  }
  return NULL;
}

cap_broker_result_t broker_svc_approve_h(cap_subject_id_t approver_subject_id,
                                         cap_share_id_t   share_id,
                                         cap_subject_id_t recipient_subject_id,
                                         capability_id_t  capability_id,
                                         cap_handle_t     owner_broker_handle,
                                         cap_handle_t    *out_recipient_handle) {
  if (out_recipient_handle != NULL) {
    *out_recipient_handle = CAP_HANDLE_NULL;
  }
  cap_broker_result_t rc = cap_broker_approve(approver_subject_id, share_id);
  if (rc != CAP_BROKER_OK) {
    return rc;
  }

  /* Mint the recipient-side handle as a CHILD of the owner's broker
   * port handle, so cap_handle_revoke_subtree(owner_broker_handle)
   * will sweep this row. Idempotent on duplicate (recipient, cap)
   * grants per cap_handle_grant_child's contract. */
  cap_handle_t child = cap_handle_grant_child(recipient_subject_id,
                                              capability_id,
                                              owner_broker_handle);
  if (child == CAP_HANDLE_NULL) {
    /* Cap-handle table refusal (bad subject, table full). The
     * cap_table grant performed by cap_broker_approve has already
     * landed; the recipient still holds the underlying cap, but no
     * handle was minted. Surface as INVALID_CAPABILITY so the
     * caller knows the handle-layer step failed. */
    return CAP_BROKER_ERR_INVALID_CAPABILITY;
  }

  /* Record in the broker_svc side-table. If the table is full we
   * silently drop the bookkeeping (the handle is still live and
   * still parent-linked via the cap_handle row itself — the
   * subtree walker reaches it through the row's parent_handle, not
   * through this side-table). The side-table is for cascade
   * accounting + audit, not for correctness of the revoke. */
  broker_svc_share_row_t *row = broker_svc_find_free_row();
  if (row != NULL) {
    row->share_id          = share_id;
    row->owner_subject     = approver_subject_id;
    row->recipient_subject = recipient_subject_id;
    row->child_handle      = child;
  }

  if (out_recipient_handle != NULL) {
    *out_recipient_handle = child;
  }
  return CAP_BROKER_OK;
}

int cap_broker_delete_owner_check(cap_subject_id_t actor_subject_id,
                                  cap_subject_id_t owner_subject_id) {
  /* ALLOW: self-delete or admin override. */
  if (actor_subject_id == owner_subject_id) {
    return 1;
  }
  if (actor_subject_id == (cap_subject_id_t)SUBJECT_M5_ADMIN) {
    return 1;
  }

  /* DENY: emit the canonical CAP:DENY marker exactly once and
   * return 0. capability_id field reuses CAP_CAPABILITY_ADMIN
   * (already registered in cdm_cap_names[]); the resource encodes
   * the would-be owner id under the literal "delete_owner_" prefix.
   *
   * Mirrors the marker-reuse rationale documented in
   * kernel/proc/process.c for the proc_table_full deny. */
  char resource[CAP_DENY_RESOURCE_MAX];
  /* Freestanding-safe "delete_owner_<u32>" formatter — broker_svc.c is
   * compiled into the kernel without libc, so no snprintf/itoa here. */
  static const char k_prefix[] = "delete_owner_";
  size_t pi = 0u;
  for (; pi < sizeof(k_prefix) - 1u && pi < sizeof(resource) - 1u; ++pi) {
    resource[pi] = k_prefix[pi];
  }
  /* Render the subject id as decimal into a small scratch then copy in. */
  char digits[11];  /* uint32_t max = 4294967295 → 10 chars + NUL */
  size_t di = 0u;
  unsigned v = (unsigned)owner_subject_id;
  if (v == 0u) {
    digits[di++] = '0';
  } else {
    char rev[11];
    size_t ri = 0u;
    while (v > 0u && ri < sizeof(rev)) {
      rev[ri++] = (char)('0' + (v % 10u));
      v /= 10u;
    }
    while (ri > 0u && di < sizeof(digits)) {
      digits[di++] = rev[--ri];
    }
  }
  for (size_t i = 0u; i < di && pi < sizeof(resource) - 1u; ++i, ++pi) {
    resource[pi] = digits[i];
  }
  resource[pi] = '\0';

  char buf[CAP_DENY_MARKER_MAX];
  int n = cap_deny_marker_format(actor_subject_id,
                                 CAP_CAPABILITY_ADMIN,
                                 resource,
                                 buf, sizeof(buf));
#if __STDC_HOSTED__
  if (n > 0) {
    (void)fwrite(buf, 1u, (size_t)n, stdout);
  }
#else
  (void)n;
  (void)buf;
#endif
  return 0;
}

broker_svc_result_t broker_svc_delete_owner(cap_subject_id_t actor_subject_id,
                                            cap_subject_id_t owner_subject_id,
                                            cap_handle_t owner_broker_handle,
                                            process_id_t owner_pid,
                                            uint32_t *out_n) {
  if (out_n != NULL) {
    *out_n = 0u;
  }

  /* Step 1: authority check. The predicate emits the canonical
   * CAP:DENY marker on rejection. */
  if (cap_broker_delete_owner_check(actor_subject_id, owner_subject_id) == 0) {
    return BROKER_SVC_ERR_DELETE_DENIED;
  }

  /* Step 2: walk the broker_svc side-table; count + (would-)audit
   * every share that descends from this owner. Audit emission is a
   * SKIP today — gated on #98 (cap.revoked.cascade event class).
   * The walk also clears the side-table row so a subsequent cascade
   * over the same owner is a clean no-op. */
  uint32_t n_children = 0u;
  for (size_t i = 0u; i < CAP_BROKER_MAX_SHARES; ++i) {
    if (g_broker_shares[i].child_handle != CAP_HANDLE_NULL &&
        g_broker_shares[i].owner_subject == owner_subject_id) {
      /* TODO(#98): emit cap.revoked.cascade {owner, recipient,
       * share_id} structured audit event here. */
      n_children = (uint32_t)(n_children + 1u);
      g_broker_shares[i].share_id          = CAP_SHARE_ID_INVALID;
      g_broker_shares[i].owner_subject     = 0u;
      g_broker_shares[i].recipient_subject = 0u;
      g_broker_shares[i].child_handle      = CAP_HANDLE_NULL;
    }
  }

  /* Step 3: load-bearing cascade. Revokes the owner's broker-port
   * handle and every recipient-side handle that was minted as a
   * child of it via broker_svc_approve_h. */
  (void)cap_handle_revoke_subtree(owner_broker_handle);

  /* Step 4: tear down the owner PCB if one was supplied.
   * process_destroy already bulk-revokes every cap handle owned by
   * the subject (#240), giving us defense-in-depth against rows the
   * subtree walk could miss (any cap_handle_grant call site that
   * didn't go through cap_handle_grant_child / broker_svc_approve_h
   * is sentinel-parented and therefore subtree-immune). */
  if (owner_pid != PID_INVALID) {
    (void)process_destroy(owner_pid);
  }

  /* Step 5: summary audit (SKIP — gated on #98). */
  /* TODO(#98): emit cap.cascade.done {owner, n_children}. */

  /* Step 6: surface the cascade count. */
  if (out_n != NULL) {
    *out_n = n_children;
  }
  return BROKER_SVC_OK;
}

#endif  /* __STDC_HOSTED__ */
