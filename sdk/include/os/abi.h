/**
 * @file abi.h
 * @brief Public SDK re-export of the SecureOS ABI version pin.
 *
 * Purpose:
 *   Slice 1 of M6 (issue #369, plan #136) — gives external apps a
 *   single header inside `sdk/include/os/` from which they can read
 *   the packed `OS_ABI_VERSION` and its `MAJOR`/`MINOR`/`PATCH`
 *   components without ever including a non-SDK path.
 *
 * Source of truth:
 *   The macros live in `user/include/secureos_abi.h` per
 *   `BUILD_ROADMAP.md` §7. This file MUST NOT mint new constants —
 *   it only re-exports what the kernel already defines so the SDK
 *   and the kernel cannot drift apart.
 *
 *   The in-tree header advertises only `OS_ABI_VERSION_MAJOR` and
 *   `OS_ABI_VERSION_MINOR` plus the packed
 *   `((MAJOR << 16) | MINOR)` constant. To satisfy the SDK's
 *   semantic-version surface (`MAJOR.MINOR.PATCH`), this file
 *   synthesises a `PATCH` of 0; PATCH is reserved for additive,
 *   SDK-only (non-ABI) tweaks and stays at 0 until the SDK beta in
 *   §7 explicitly assigns it meaning.
 *
 * Containment:
 *   This header is freestanding-safe: no libc, no `kernel/` includes.
 *   The `validate_sdk_no_kernel_includes` CI check enforces that no
 *   future SDK source pulls in kernel-only paths.
 *
 * Launched by:
 *   Header-only; consumed by external SDK consumers and by
 *   `tests/sdk_abi_pin_test.c`.
 */

#ifndef OS_ABI_H
#define OS_ABI_H

/*
 * The in-tree source of truth is `user/include/secureos_abi.h`. External
 * consumers vendor or `-isystem` the SDK include path; in-tree consumers
 * (and the `sdk_abi_pin` test) compile with both `-Isdk/include` and
 * `-Iuser/include` so the bare include resolves to the kernel ABI header.
 */
#include "secureos_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Re-export the in-tree ABI version macros under the public SDK
 * spelling. We intentionally use `#ifndef` guards so a future slice
 * (or an embedder vendoring the SDK) cannot accidentally double-define
 * the names: if `secureos_abi.h` already defined them (it does), these
 * become no-ops, and the SDK silently inherits the kernel value.
 */
#ifndef OS_ABI_VERSION_MAJOR
#error "OS_ABI_VERSION_MAJOR missing from secureos_abi.h (SDK source of truth)"
#endif
#ifndef OS_ABI_VERSION_MINOR
#error "OS_ABI_VERSION_MINOR missing from secureos_abi.h (SDK source of truth)"
#endif

/*
 * PATCH is SDK-only and reserved; it is NOT advertised by the in-tree
 * `secureos_abi.h` because the kernel ABI itself is `MAJOR.MINOR` only
 * (per BUILD_ROADMAP §7). Slice 1 fixes it at 0; if a future slice
 * needs to bump it (e.g. for a non-ABI SDK header tweak), the value
 * here changes and `sdk/VERSION` must move in lockstep — the
 * `sdk_abi_pin` test enforces both.
 */
#ifndef OS_ABI_VERSION_PATCH
#define OS_ABI_VERSION_PATCH 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* OS_ABI_H */
