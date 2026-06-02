/**
 * @file app_native_heap.h
 * @brief Per-app native heap window backing the `mem_brk` bridge slot
 *        wired into `bridge->mem_brk` by `launcher_exec.c`.
 *
 * M7-TOOLCHAIN-001 slice 2 (#421) extracted into its own translation
 * unit by the QEMU end-to-end peer (#495) so the production heap
 * implementation can be link-time exercised by the
 * `mem_brk_qemu_test` round-trip without dragging the whole
 * launcher_exec.c HAL/crypto/IPC dependency surface into the host
 * test build (same separation pattern as `kernel/cap/cap_*.c`).
 *
 * Contract — identical to the static helper that used to live inline
 * in launcher_exec.c:
 *
 *   - `app_native_mem_brk(delta, out_prev_break)` is the body of the
 *     `mem_brk` bridge slot. Returns 0 (`OS_STATUS_OK`), 1
 *     (`OS_STATUS_DENIED` — out-of-arena, never panics) or 3
 *     (`OS_STATUS_ERROR` — NULL out-pointer). On success writes the
 *     *previous* break (POSIX `sbrk(2)` shape) into `*out_prev_break`.
 *   - `app_native_heap_reset()` rewinds the break to 0 for a fresh
 *     per-process bridge wire. Called by launcher_exec on every
 *     non-nested bridge install.
 *   - `APP_NATIVE_HEAP_BYTES` is the fixed BSS pool ceiling that
 *     bounds the `os_mem_brk` cap for the in-tree native runtime.
 *
 * Same constants used by the BSS pool (4 MiB) — see launcher_exec.c
 * §"M7-TOOLCHAIN-001" for the sizing rationale.
 */

#ifndef SECUREOS_KERNEL_USER_APP_NATIVE_HEAP_H
#define SECUREOS_KERNEL_USER_APP_NATIVE_HEAP_H

#include <stddef.h>

#define APP_NATIVE_HEAP_BYTES (4u * 1024u * 1024u)

/* sbrk(2)-shape grow / shrink / query against the per-app pool.
 * Return values mirror `os_status_t` (0/1/3) so the launcher bridge
 * slot can pass the int through unchanged. */
int  app_native_mem_brk(int delta, void **out_prev_break);

/* Reset the per-process break to 0 on a fresh top-level bridge wire. */
void app_native_heap_reset(void);

/* Test-only accessor returning the current break offset (bytes from
 * the pool base). Lets the `_qemu` round-trip peer assert the
 * arena-reset contract without poking the static directly. */
size_t app_native_heap_break_for_tests(void);

#endif /* SECUREOS_KERNEL_USER_APP_NATIVE_HEAP_H */
