/**
 * @file ipc_port_lifecycle_test.c
 * @brief Lifecycle round-trip test for the M1 IPC port table (#223).
 *
 * Asserts the four invariants spelled out in issue #223:
 *
 *   1. create → destroy → create returns a *different* handle (the
 *      generation counter advanced) even when the underlying slot
 *      index is reused.
 *   2. The pre-destroy (stale) handle returns IPC_ERR_INVALID_PORT
 *      on every read accessor (owner/send_cap/recv_cap) after destroy.
 *   3. ipc_port_destroy(IPC_PORT_INVALID) → IPC_ERR_INVALID_PORT (no
 *      underflow / no-op crash).
 *   4. Generation never returns to 0 (handle is non-zero by
 *      construction) even after wrap-around: simulate wrap by
 *      65535 create/destroy cycles on the same slot index.
 *
 * This is intentionally a docs/test lock-in for the existing
 * kernel/ipc/ipc_port.{c,h} surface — no API changes here.
 *
 * Launched by:
 *   build/scripts/test_ipc_port_lifecycle.sh (dispatched via
 *   build/scripts/test.sh ipc_port_lifecycle).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_port.h"

static void die(const char *reason) {
  printf("TEST:FAIL:ipc_port_lifecycle:%s\n", reason);
  exit(1);
}

static void reset_world(void) {
  ipc_port_table_reset();
}

static ipc_port_t create_or_die(const char *where) {
  ipc_port_t p = IPC_PORT_INVALID;
  ipc_result_t rc = ipc_port_create((cap_subject_id_t)1,
                                    CAP_IPC_SEND,
                                    CAP_IPC_RECV,
                                    &p);
  if (rc != IPC_OK) {
    die(where);
  }
  if (p == IPC_PORT_INVALID) {
    die("create_returned_invalid_handle");
  }
  return p;
}

/* Check 1: create → destroy → create yields a different handle, and
 * the underlying slot index is reused (slot reuse is exercised by the
 * implementation's first-fit walk; in an otherwise-empty table the
 * second create will land on the same index, so the only bit that may
 * legitimately differ is the generation field). */
static void check_handle_advances_on_reuse(void) {
  reset_world();
  ipc_port_t a = create_or_die("first_create");
  if (ipc_port_destroy(a) != IPC_OK) {
    die("destroy_a");
  }
  ipc_port_t b = create_or_die("second_create");
  if (a == b) {
    die("handle_did_not_advance_after_destroy_create");
  }
  /* Slot-reuse + generation-advance contract: lower 16 bits are the
   * table index, upper 16 are the generation. In an empty table the
   * new create MUST reuse the same slot index, so the only differing
   * bits MUST be in the generation half. */
  uint16_t a_idx = (uint16_t)(a & 0xFFFFu);
  uint16_t b_idx = (uint16_t)(b & 0xFFFFu);
  uint16_t a_gen = (uint16_t)((a >> 16) & 0xFFFFu);
  uint16_t b_gen = (uint16_t)((b >> 16) & 0xFFFFu);
  if (a_idx != b_idx) {
    die("reused_slot_index_changed");
  }
  if (b_gen == a_gen) {
    die("generation_did_not_advance");
  }
  /* Cleanup. */
  if (ipc_port_destroy(b) != IPC_OK) {
    die("destroy_b");
  }
  printf("TEST:PASS:ipc_port_lifecycle_handle_advances\n");
}

/* Check 2: every read accessor rejects the stale (pre-destroy)
 * handle with IPC_ERR_INVALID_PORT. */
static void check_stale_handle_rejected(void) {
  reset_world();
  ipc_port_t stale = create_or_die("stale_create");
  if (ipc_port_destroy(stale) != IPC_OK) {
    die("destroy_stale");
  }

  cap_subject_id_t owner = 0u;
  if (ipc_port_owner(stale, &owner) != IPC_ERR_INVALID_PORT) {
    die("stale_owner_not_rejected");
  }
  capability_id_t cap = (capability_id_t)0;
  if (ipc_port_send_cap(stale, &cap) != IPC_ERR_INVALID_PORT) {
    die("stale_send_cap_not_rejected");
  }
  if (ipc_port_recv_cap(stale, &cap) != IPC_ERR_INVALID_PORT) {
    die("stale_recv_cap_not_rejected");
  }
  /* Double-destroy of a stale handle is also a transport fault. */
  if (ipc_port_destroy(stale) != IPC_ERR_INVALID_PORT) {
    die("stale_double_destroy_not_rejected");
  }
  printf("TEST:PASS:ipc_port_lifecycle_stale_rejected\n");
}

/* Check 3: destroying the reserved-invalid handle is a no-op-crash
 * deterministic IPC_ERR_INVALID_PORT. */
static void check_destroy_invalid_handle(void) {
  reset_world();
  if (ipc_port_destroy(IPC_PORT_INVALID) != IPC_ERR_INVALID_PORT) {
    die("destroy_invalid_did_not_return_invalid_port");
  }
  printf("TEST:PASS:ipc_port_lifecycle_destroy_invalid\n");
}

/* Check 4: wrap-around. Run more create/destroy cycles than the
 * 16-bit generation field can hold and assert (a) every issued handle
 * is non-zero and (b) the slot index never changes (no leak/overflow
 * into the index half), confirming the `if (gen == 0) gen = 1` guard
 * in ipc_port.c keeps IPC_PORT_INVALID unreachable forever. */
static void check_generation_wrap_around(void) {
  reset_world();
  ipc_port_t first = create_or_die("wrap_first_create");
  uint16_t fixed_index = (uint16_t)(first & 0xFFFFu);
  if (ipc_port_destroy(first) != IPC_OK) {
    die("wrap_first_destroy");
  }

  /* 65535 cycles + the initial pair guarantees we cross the
   * generation-counter wrap exactly once. */
  for (uint32_t i = 0; i < 65535u; ++i) {
    ipc_port_t p = IPC_PORT_INVALID;
    ipc_result_t rc = ipc_port_create((cap_subject_id_t)1,
                                      CAP_IPC_SEND,
                                      CAP_IPC_RECV,
                                      &p);
    if (rc != IPC_OK) {
      die("wrap_cycle_create_failed");
    }
    if (p == IPC_PORT_INVALID) {
      die("wrap_cycle_handle_was_invalid");
    }
    if ((uint16_t)(p & 0xFFFFu) != fixed_index) {
      die("wrap_cycle_slot_index_drifted");
    }
    if (ipc_port_destroy(p) != IPC_OK) {
      die("wrap_cycle_destroy_failed");
    }
  }
  printf("TEST:PASS:ipc_port_lifecycle_wrap_around\n");
}

int main(void) {
  check_handle_advances_on_reuse();
  check_stale_handle_rejected();
  check_destroy_invalid_handle();
  check_generation_wrap_around();
  printf("TEST:PASS:ipc_port_lifecycle\n");
  return 0;
}
