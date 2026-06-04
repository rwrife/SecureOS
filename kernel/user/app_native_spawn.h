/**
 * @file app_native_spawn.h
 * @brief CAP_APP_EXEC pre-check + canonical `CAP:DENY:<sid>:app_exec:<resource>`
 *        emission for the `app_native_process_spawn` bridge slot
 *        (M7-TOOLCHAIN-003, #422 / PR #427).
 *
 * Extracted from `kernel/user/launcher_exec.c` so the load-bearing deny
 * marker that `launch.denied` (plan #403 P4, BUILD_ROADMAP §5.2) and the
 * `toolchain_unsigned_prompt_enforced` acceptance (#410) lean on can be
 * host-link tested without dragging the launcher_exec HAL/IPC/crypto
 * dependency surface into the unit-test build.
 *
 * Same extraction pattern as `kernel/user/app_native_heap.c` (PR #495,
 * #421) used for `app_native_mem_brk`.
 *
 * Contract — identical to the static helper that used to live inline in
 * `app_native_process_spawn` in launcher_exec.c:
 *
 *   - If the subject holds `CAP_APP_EXEC`, returns 0; no I/O, no marker.
 *   - Otherwise emits exactly one byte-exact line of the form
 *       CAP:DENY:<subject>:app_exec:<resource>\n
 *     to stdout (when __STDC_HOSTED__) and returns 1.
 *   - `<resource>` is `path` sanitized: any ':' / newline / non-printable
 *     byte ([0x00..0x1F] or 0x7F..) is replaced with '_'. The sanitized
 *     resource is truncated at CAP_DENY_RESOURCE_MAX bytes. An empty or
 *     NULL path produces a single '_' resource so the marker still
 *     round-trips through cap_deny_marker_validate().
 *   - A NULL `path` is treated as the empty path for sanitization; the
 *     caller (launcher) rejects NULL/empty earlier and returns 3, so the
 *     marker never names an empty resource in production, but the
 *     fallback keeps the helper's deny-emit invariant total.
 *
 * Pinned by `tests/app_native_process_spawn_deny_marker_test.c` (#532).
 */

#ifndef SECUREOS_KERNEL_USER_APP_NATIVE_SPAWN_H
#define SECUREOS_KERNEL_USER_APP_NATIVE_SPAWN_H

#include "../cap/capability.h"

/**
 * Pre-check `CAP_APP_EXEC` for `subject_id` and, on missing capability,
 * emit the canonical `CAP:DENY:<subject>:app_exec:<resource>` marker.
 *
 * @param subject_id  Subject id the spawn is being attempted as.
 * @param path        Target binary path. Sanitized for the resource
 *                    field per the rules in the file header. NULL is
 *                    treated as the empty path.
 * @return 0 when the subject holds CAP_APP_EXEC (caller continues),
 *         1 when the subject does NOT hold CAP_APP_EXEC (marker
 *         emitted; caller returns PROCESS_ERR_CAPABILITY-equivalent
 *         to the userland wrapper).
 */
int app_native_spawn_cap_check(cap_subject_id_t subject_id, const char *path);

#endif /* SECUREOS_KERNEL_USER_APP_NATIVE_SPAWN_H */
