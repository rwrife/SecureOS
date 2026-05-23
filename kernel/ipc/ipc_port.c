/**
 * @file ipc_port.c
 * @brief Port table implementation for the M1 synchronous IPC v0 scaffold.
 *
 * Purpose:
 *   Maintains a small fixed-size array of port slots indexed by a 16-bit
 *   table index, with a 16-bit generation counter packed into the high
 *   half of `ipc_port_t` to detect use-after-destroy. Each slot holds
 *   the owning subject, the send/recv capability ids, and a single
 *   "in flight" envelope used to emulate the synchronous rendezvous
 *   described by docs/abi/ipc-wire.md §4 (issue #210).
 *
 *   This file deliberately contains no synchronization primitives: the
 *   v0 scaffold is single-threaded and lives between two in-kernel test
 *   modules. A scheduler-aware blocking implementation is a follow-up
 *   (see plans/2026-05-19-m1-sync-ipc-primitive.md non-goals).
 *
 * Interactions:
 *   - ipc_port.h: public interface implemented here.
 *   - ipc_ops.c: the only caller of ipc_port_stage/consume in normal use.
 *
 * Launched by:
 *   Not a standalone process. Compiled into the kernel image and into
 *   the host-side ipc_sync_v0 test binary.
 *
 * Issue: #210.
 */

#include "ipc_port.h"

#include <string.h>

typedef struct {
  bool live;
  bool slot_occupied;
  uint16_t generation;
  cap_subject_id_t owner;
  capability_id_t send_cap;
  capability_id_t recv_cap;
  ipc_msg_v0 staged;
} ipc_port_slot_t;

static ipc_port_slot_t g_ports[IPC_PORT_TABLE_MAX];
static bool g_table_initialized = false;

#define IPC_PORT_INDEX_MASK 0xFFFFu
#define IPC_PORT_GEN_SHIFT 16u

static ipc_port_t encode_handle(uint16_t index, uint16_t generation) {
  /* Index 0 with generation 0 collides with IPC_PORT_INVALID, so we
   * always start generations at 1 and bump on each (re)use. */
  return ((ipc_port_t)generation << IPC_PORT_GEN_SHIFT) | (ipc_port_t)index;
}

static bool decode_handle(ipc_port_t handle, uint16_t *out_index, uint16_t *out_gen) {
  if (handle == IPC_PORT_INVALID) {
    return false;
  }
  *out_index = (uint16_t)(handle & IPC_PORT_INDEX_MASK);
  *out_gen = (uint16_t)((handle >> IPC_PORT_GEN_SHIFT) & 0xFFFFu);
  return *out_index < IPC_PORT_TABLE_MAX;
}

static ipc_port_slot_t *resolve(ipc_port_t handle) {
  uint16_t index = 0u;
  uint16_t gen = 0u;
  if (!decode_handle(handle, &index, &gen)) {
    return NULL;
  }
  ipc_port_slot_t *slot = &g_ports[index];
  if (!slot->live || slot->generation != gen) {
    return NULL;
  }
  return slot;
}

static bool capability_id_known(capability_id_t cap) {
  switch (cap) {
    case CAP_CONSOLE_WRITE:
    case CAP_SERIAL_WRITE:
    case CAP_DEBUG_EXIT:
    case CAP_CAPABILITY_ADMIN:
    case CAP_DISK_IO_REQUEST:
    case CAP_FS_READ:
    case CAP_FS_WRITE:
    case CAP_EVENT_SUBSCRIBE:
    case CAP_EVENT_PUBLISH:
    case CAP_APP_EXEC:
    case CAP_CODESIGN_BYPASS:
    case CAP_NETWORK:
    case CAP_IPC_SEND:
    case CAP_IPC_RECV:
    case CAP_SYSCALL:
      return true;
  }
  return false;
}

void ipc_port_table_init(void) {
  if (!g_table_initialized) {
    memset(g_ports, 0, sizeof(g_ports));
    /* Start generations at 1 so encode_handle never produces 0 for
     * index 0 (which would alias IPC_PORT_INVALID). */
    for (size_t i = 0; i < IPC_PORT_TABLE_MAX; ++i) {
      g_ports[i].generation = 1u;
    }
    g_table_initialized = true;
    return;
  }
  ipc_port_table_reset();
}

void ipc_port_table_reset(void) {
  for (size_t i = 0; i < IPC_PORT_TABLE_MAX; ++i) {
    if (g_ports[i].live || g_ports[i].slot_occupied) {
      g_ports[i].generation = (uint16_t)(g_ports[i].generation + 1u);
      if (g_ports[i].generation == 0u) {
        g_ports[i].generation = 1u;
      }
    } else if (g_ports[i].generation == 0u) {
      g_ports[i].generation = 1u;
    }
    g_ports[i].live = false;
    g_ports[i].slot_occupied = false;
    g_ports[i].owner = 0u;
    g_ports[i].send_cap = (capability_id_t)0;
    g_ports[i].recv_cap = (capability_id_t)0;
    memset(&g_ports[i].staged, 0, sizeof(g_ports[i].staged));
  }
  g_table_initialized = true;
}

ipc_result_t ipc_port_create(cap_subject_id_t owner,
                             capability_id_t send_cap,
                             capability_id_t recv_cap,
                             ipc_port_t *out_port) {
  if (!g_table_initialized) {
    ipc_port_table_init();
  }
  if (out_port == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  if (!capability_id_known(send_cap) || !capability_id_known(recv_cap)) {
    return IPC_ERR_INVALID_MSG;
  }
  *out_port = IPC_PORT_INVALID;

  for (uint16_t i = 0; i < IPC_PORT_TABLE_MAX; ++i) {
    if (!g_ports[i].live) {
      g_ports[i].live = true;
      g_ports[i].slot_occupied = false;
      g_ports[i].owner = owner;
      g_ports[i].send_cap = send_cap;
      g_ports[i].recv_cap = recv_cap;
      memset(&g_ports[i].staged, 0, sizeof(g_ports[i].staged));
      *out_port = encode_handle(i, g_ports[i].generation);
      return IPC_OK;
    }
  }
  return IPC_ERR_INVALID_PORT;
}

ipc_result_t ipc_port_destroy(ipc_port_t port) {
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  slot->live = false;
  slot->slot_occupied = false;
  slot->generation = (uint16_t)(slot->generation + 1u);
  if (slot->generation == 0u) {
    slot->generation = 1u;
  }
  memset(&slot->staged, 0, sizeof(slot->staged));
  return IPC_OK;
}

ipc_result_t ipc_port_owner(ipc_port_t port, cap_subject_id_t *out_owner) {
  if (out_owner == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  *out_owner = slot->owner;
  return IPC_OK;
}

ipc_result_t ipc_port_send_cap(ipc_port_t port, capability_id_t *out_cap) {
  if (out_cap == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  *out_cap = slot->send_cap;
  return IPC_OK;
}

ipc_result_t ipc_port_recv_cap(ipc_port_t port, capability_id_t *out_cap) {
  if (out_cap == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  *out_cap = slot->recv_cap;
  return IPC_OK;
}

ipc_result_t ipc_port_stage(ipc_port_t port, const ipc_msg_v0 *msg) {
  if (msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  if (slot->slot_occupied) {
    /* Single-waiter slot: a second staged envelope without an
     * intervening consume is a transport fault in v0. The strict
     * blocking variant is a follow-up (ipc-wire.md §4). */
    return IPC_ERR_PEER_GONE;
  }
  memcpy(&slot->staged, msg, sizeof(*msg));
  slot->slot_occupied = true;
  return IPC_OK;
}

ipc_result_t ipc_port_consume(ipc_port_t port, ipc_msg_v0 *out_msg) {
  if (out_msg == NULL) {
    return IPC_ERR_INVALID_MSG;
  }
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return IPC_ERR_INVALID_PORT;
  }
  if (!slot->slot_occupied) {
    return IPC_ERR_PEER_GONE;
  }
  memcpy(out_msg, &slot->staged, sizeof(*out_msg));
  slot->slot_occupied = false;
  memset(&slot->staged, 0, sizeof(slot->staged));
  return IPC_OK;
}

bool ipc_port_has_pending_for_tests(ipc_port_t port) {
  ipc_port_slot_t *slot = resolve(port);
  if (slot == NULL) {
    return false;
  }
  return slot->slot_occupied;
}

const void *ipc_port_wait_token(ipc_port_t port) {
  /* Return the slot pointer itself. Stable for the lifetime of the
   * handle's generation and unique per live port. Callers must treat
   * it as an opaque equality token only. */
  return (const void *)resolve(port);
}
