/**
 * @file syscall_entry.c
 * @brief M1 stub implementation of `kernel_syscall_entry` (issue #232).
 *
 * Behavior (per plan #198, "stubbed but unused"):
 *
 *   - Every vector — in-range or out-of-range — returns
 *     `IPC_ERR_INVALID_MSG`.
 *   - Every invocation emits a single `CAP:DENY:<actor>:syscall:-`
 *     line via the canonical formatter in `kernel/cap/cap_deny_marker.c`.
 *     The formatter is the single source of truth for the marker
 *     grammar (#211); we deliberately do *not* `printf` the line by
 *     hand here so a future change to the marker shape only has to
 *     touch one place.
 *
 * No GDT/TSS/ring-3 plumbing in this file — that is M2+ work and
 * explicitly out of scope per issue #232.
 *
 * Used by:
 *   - tests/syscall_entry_stub_test.c (this issue).
 *   - The future ring-3 transition will replace the body of this
 *     function with a real dispatch table while keeping the deny path
 *     for unknown / unauthorized vectors intact.
 *
 * Launched by:
 *   In M1, only the unit-test harness. The kernel image links the
 *   object so the symbol is present, but no in-kernel call site
 *   exists yet.
 */

#include "syscall_entry.h"

#include "../cap/cap_deny_marker.h"

#include <stdio.h>

/*
 * Spec-mandated canonical resource string for a deny marker that has
 * no per-call resource handle, matching the value already used by the
 * IPC deny path (`kernel/ipc/ipc_ops.c`).
 */
#define SYSCALL_DENY_RESOURCE_NONE "-"

static void syscall_emit_deny_marker(cap_subject_id_t caller_subject) {
  char buf[CAP_DENY_MARKER_MAX];
  int n = cap_deny_marker_format(caller_subject,
                                 CAP_SYSCALL,
                                 SYSCALL_DENY_RESOURCE_NONE,
                                 buf,
                                 sizeof(buf));
  if (n <= 0) {
    /* Formatter contract violation — fall back to the literal grammar
     * so the kernel still emits *something* recognizable rather than
     * silently swallowing a deny. The shape test will fail loudly. */
    printf("CAP:DENY:%u:syscall:%s\n",
           (unsigned)caller_subject,
           SYSCALL_DENY_RESOURCE_NONE);
    return;
  }
  /* cap_deny_marker_format() already appends '\n'; fwrite to stdout so
   * we do not accidentally inject an extra newline via printf("%s\n"). */
  (void)fwrite(buf, 1u, (size_t)n, stdout);
}

ipc_result_t kernel_syscall_entry(cap_subject_id_t caller_subject,
                                  uint32_t vector,
                                  uintptr_t arg0,
                                  uintptr_t arg1,
                                  uintptr_t arg2) {
  (void)vector;
  (void)arg0;
  (void)arg1;
  (void)arg2;

  /* M1 contract: every vector is unimplemented. Emit the deny marker
   * unconditionally so the deny-marker shape contract applies the
   * moment any caller wires up. */
  syscall_emit_deny_marker(caller_subject);

  return IPC_ERR_INVALID_MSG;
}
