/**
 * @file app_native_spawn.c
 * @brief CAP_APP_EXEC pre-check + canonical deny-marker emit for the
 *        `app_native_process_spawn` bridge slot. See app_native_spawn.h
 *        for contract.
 *
 * Extracted from `kernel/user/launcher_exec.c` (M7-TOOLCHAIN-003, #422 /
 * PR #427) so the canonical `CAP:DENY:<sid>:app_exec:<resource>` line
 * can be host-link pinned by `tests/app_native_process_spawn_deny_marker_test.c`
 * (#532) without pulling launcher_exec's HAL/IPC/crypto deps into the
 * test build. Same extraction pattern as `app_native_heap.c` (#495).
 */

#include "app_native_spawn.h"

#include "../cap/cap_deny_marker.h"
#include "../cap/cap_table.h"

#if __STDC_HOSTED__
#include <stdio.h>
#endif

int app_native_spawn_cap_check(cap_subject_id_t subject_id, const char *path) {
  if (cap_table_check(subject_id, CAP_APP_EXEC) == CAP_OK) {
    return 0;
  }

  /* Sanitize: cap_deny_marker_format rejects ':' / newline / non-
   * printable bytes in the resource field. Truncate at the resource
   * cap and replace any forbidden byte with '_' so a pathological path
   * can still produce a parseable marker. NULL path is treated as
   * empty (production callers reject NULL upstream; the empty fallback
   * below keeps the helper's deny-emit invariant total). */
  char resource[CAP_DENY_RESOURCE_MAX + 1u];
  size_t i = 0u;
  if (path != 0) {
    for (i = 0u; i < CAP_DENY_RESOURCE_MAX && path[i] != '\0'; ++i) {
      unsigned char c = (unsigned char)path[i];
      if (c == ':' || c == '\n' || c < 0x20u || c > 0x7Eu) {
        resource[i] = '_';
      } else {
        resource[i] = (char)c;
      }
    }
  }
  if (i == 0u) {
    resource[i++] = '_';
  }
  resource[i] = '\0';

  char marker[CAP_DENY_MARKER_MAX];
  int n = cap_deny_marker_format(subject_id, CAP_APP_EXEC,
                                 resource, marker, sizeof(marker));
#if __STDC_HOSTED__
  if (n > 0) {
    (void)fwrite(marker, 1u, (size_t)n, stdout);
  }
#else
  (void)n;
  (void)marker;
#endif
  return 1;
}
