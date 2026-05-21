/**
 * @file ipc_msg.h
 * @brief SecureOS synchronous IPC v0 wire envelope and error model.
 *
 * Purpose:
 *   Single source of truth for the on-wire IPC envelope (`ipc_msg_v0`)
 *   and the frozen `ipc_result_t` error vocabulary defined by
 *   `docs/abi/ipc-wire.md` for `OS_ABI_VERSION = 0`. This is the M1
 *   synchronous IPC primitive v0 ABI surface (issue #210, plan
 *   `plans/2026-05-19-m1-sync-ipc-primitive.md`).
 *
 *   The struct layout is fixed by the spec:
 *     - 16-byte header (abi_version, flags, sender_subject, tag,
 *       payload_len) followed by a fixed 64-byte payload region.
 *     - Total envelope size: 80 bytes. All multi-byte fields are
 *       naturally aligned and little-endian on supported targets.
 *     - `flags` is reserved (must be zero in v0).
 *     - `sender_subject` is kernel-stamped on send; receivers MUST
 *       reject envelopes whose sender_subject == 0 with
 *       IPC_ERR_INVALID_MSG.
 *
 * Interactions:
 *   - kernel/ipc/ipc_port.{c,h}: port table; envelopes are staged in a
 *     single-waiter slot per port for the v0 in-kernel rendezvous.
 *   - kernel/ipc/ipc_ops.c: implements ipc_send / ipc_recv / ipc_call
 *     and is the only writer of `sender_subject` on outbound frames.
 *   - kernel/cap/capability.h: `cap_subject_id_t` is the type stamped
 *     into `sender_subject`; CAP_IPC_SEND / CAP_IPC_RECV gate the ops.
 *   - user/include/secureos_abi.h: provides `OS_ABI_VERSION` that every
 *     envelope carries and that receivers cross-check.
 *
 * Launched by:
 *   Header-only; not a standalone process. Compiled into the kernel
 *   image and into the host-side IPC test binary
 *   (`tests/ipc_sync_v0_test.c`).
 *
 * Issue: #210. Plan: plans/2026-05-19-m1-sync-ipc-primitive.md.
 */

#ifndef SECUREOS_KERNEL_IPC_MSG_H
#define SECUREOS_KERNEL_IPC_MSG_H

#include <stddef.h>
#include <stdint.h>

#include "../../user/include/secureos_abi.h"
#include "../cap/capability.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fixed-size payload region of the v0 IPC envelope. Spec-frozen at 64
 * bytes by docs/abi/ipc-wire.md §2; changing this requires a bump of
 * OS_ABI_VERSION (non-additive change).
 */
#define IPC_MSG_PAYLOAD_MAX 64u

/*
 * Total envelope size in v0. Used by tests and static_asserts to ensure
 * no implicit padding has been introduced by the compiler/target.
 */
#define IPC_MSG_V0_SIZE 80u

/*
 * Reserved-as-MBZ flag mask. Any non-zero bit here on receive means the
 * envelope is malformed (IPC_ERR_INVALID_MSG).
 */
#define IPC_MSG_FLAGS_RESERVED_MBZ 0xFFFFu

/*
 * The v0 IPC message envelope. Exactly the wire layout from
 * docs/abi/ipc-wire.md §2 — do not reorder, resize, or pad.
 *
 *   offset  size  field
 *   ------  ----  -----
 *        0     2  abi_version
 *        2     2  flags             (MBZ in v0)
 *        4     4  sender_subject    (kernel-stamped on send)
 *        8     4  tag               (caller-defined; opaque to IPC)
 *       12     4  payload_len       (0..IPC_MSG_PAYLOAD_MAX)
 *       16    64  payload[]
 *   ------  ----
 *       80
 */
typedef struct ipc_msg_v0 {
  uint16_t abi_version;
  uint16_t flags;
  uint32_t sender_subject;
  uint32_t tag;
  uint32_t payload_len;
  uint8_t payload[IPC_MSG_PAYLOAD_MAX];
} ipc_msg_v0;

/*
 * Frozen-for-v0 IPC error vocabulary. Adding new entries is *not*
 * additive (see ipc-wire.md §6); a bump of OS_ABI_VERSION is required.
 *
 * Class taxonomy (ipc-wire.md §5.1):
 *   - capability decision: IPC_ERR_CAP_DENIED only.
 *   - malformed message:   IPC_ERR_INVALID_MSG.
 *   - transport fault:     IPC_ERR_INVALID_PORT, IPC_ERR_PEER_GONE.
 *   - reserved future:     IPC_ERR_WOULD_BLOCK (MUST NOT be returned by
 *                          v0 implementations; consumers MAY test for
 *                          forward compat).
 */
typedef enum {
  IPC_OK = 0,
  IPC_ERR_INVALID_PORT = 1,
  IPC_ERR_INVALID_MSG = 2,
  IPC_ERR_CAP_DENIED = 3,
  IPC_ERR_WOULD_BLOCK = 4,
  IPC_ERR_PEER_GONE = 5,
} ipc_result_t;

/*
 * Compile-time guard: catch host targets that would silently insert
 * padding into the envelope. The static_assert below is conditional
 * because freestanding kernel builds may pre-date C11 in some shims;
 * the host-side test build always exercises C11.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(ipc_msg_v0) == IPC_MSG_V0_SIZE,
               "ipc_msg_v0 must be exactly IPC_MSG_V0_SIZE bytes (no padding) — see docs/abi/ipc-wire.md §2.2");
_Static_assert(offsetof(ipc_msg_v0, abi_version) == 0,
               "ipc_msg_v0.abi_version must be at offset 0");
_Static_assert(offsetof(ipc_msg_v0, flags) == 2,
               "ipc_msg_v0.flags must be at offset 2");
_Static_assert(offsetof(ipc_msg_v0, sender_subject) == 4,
               "ipc_msg_v0.sender_subject must be at offset 4");
_Static_assert(offsetof(ipc_msg_v0, tag) == 8,
               "ipc_msg_v0.tag must be at offset 8");
_Static_assert(offsetof(ipc_msg_v0, payload_len) == 12,
               "ipc_msg_v0.payload_len must be at offset 12");
_Static_assert(offsetof(ipc_msg_v0, payload) == 16,
               "ipc_msg_v0.payload must be at offset 16");
#endif

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_KERNEL_IPC_MSG_H */
