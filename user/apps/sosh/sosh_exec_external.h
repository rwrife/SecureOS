/**
 * @file sosh_exec_external.h
 * @brief sosh embedder helper: probe-then-spawn fall-through for external
 *        commands. Closes the gap in `sosh_app_exec` left open by #422 —
 *        the syscall + wrapper landed, but `sosh> hello` still no-op'd
 *        because main.c had no fall-through onto `os_process_spawn`.
 *
 * Tracks issue #493 (sub-slice of #410, depends on #422). Pairs with the
 * existing soshlib-side SOSH_CAP_APP_EXEC gate in `sosh_eval.c`: when
 * that gate allows, soshlib calls back into `state->exec` (sosh's
 * `sosh_app_exec`); we now resolve the command name to a SOF path on
 * disk and forward to the kernel `os_process_spawn` wrapper.
 *
 * Separation of concerns:
 *   - `user/apps/sosh/main.c` provides the production probe + spawn
 *     bindings (wired to `os_fs_list_dir` / `os_process_spawn`).
 *   - `tests/sosh_external_exec_test.c` injects mocks to exercise
 *     allow / deny / unknown behaviour without a live bridge.
 *
 * The helper does not invent its own deny marker — the canonical
 * `CAP:DENY:<sid>:app_exec:<resource>` marker is emitted by the kernel
 * leg of `os_process_spawn` (per `docs/abi/syscalls.md` and
 * `docs/abi/sosh-capability-contract.md` §4). This helper just makes
 * sure the deny rc is **not swallowed** and surfaces as a non-zero
 * `sosh_app_exec` return so `$?` is meaningful in a script.
 */

#ifndef SOSH_EXEC_EXTERNAL_H
#define SOSH_EXEC_EXTERNAL_H

#include "secureos_api.h"

/* Sentinel rc values returned by `sosh_try_exec_external`. The numeric
 * choices match the long-standing POSIX-shell convention used elsewhere
 * in sosh: 127 = command not found, 126 = found-but-cannot-execute
 * (denied / spawn error). */
#define SOSH_EXEC_RC_NOT_FOUND 127
#define SOSH_EXEC_RC_DENIED    126
#define SOSH_EXEC_RC_ERROR     126

/*
 * Result of a fall-through external-exec attempt.
 *   - `SOSH_EXTERNAL_RAN`        : we resolved the command and called
 *                                  spawn; `*out_exit` carries the
 *                                  child's exit status (or the
 *                                  embedder-defined deny/error rc).
 *   - `SOSH_EXTERNAL_NOT_FOUND`  : no probe succeeded; the caller
 *                                  should fall back to its existing
 *                                  "command not found" behaviour.
 */
typedef enum {
  SOSH_EXTERNAL_RAN = 0,
  SOSH_EXTERNAL_NOT_FOUND = 1,
} sosh_external_result_t;

/* Returns OS_STATUS_OK iff `path` resolves to something on disk
 * (typical implementation: `os_fs_list_dir` then `os_fs_read_file`).
 * Implementations may take a context pointer for test-side state. */
typedef os_status_t (*sosh_path_probe_fn)(const char *path, void *ctx);

/* Spawn `path` with `argv` (NULL-terminated). Implementations are
 * expected to forward to `os_process_spawn` and pass back its
 * `os_status_t` and the child's exit status. */
typedef os_status_t (*sosh_spawn_fn)(const char *path,
                                     const char *const *argv,
                                     unsigned int flags,
                                     int *out_exit_status,
                                     void *ctx);

/*
 * Probe `<command>` on disk in the documented search order
 * (`/apps/<command>`, `/apps/dev/<command>`, then `<command>` literal)
 * and, on the first hit, forward to `spawn(resolved_path, argv, ...)`.
 *
 * Argv is built as `{ command, args, NULL }` — matching the existing
 * `os_process_spawn` wrapper contract that space-joins `argv[1..]`
 * into a single raw-args string. `args` may be NULL or empty.
 *
 * Returns `SOSH_EXTERNAL_RAN` once spawn has been attempted; the
 * caller MUST inspect `*out_exit` for the rc to surface to soshlib.
 * Returns `SOSH_EXTERNAL_NOT_FOUND` iff no probe path matched — the
 * caller is then free to keep its existing "command not found"
 * behaviour (sosh main.c prints the canonical message and returns
 * 127). The deny path (`OS_STATUS_DENIED`) must NOT be swallowed:
 * it surfaces as `SOSH_EXTERNAL_RAN` with `*out_exit = SOSH_EXEC_RC_DENIED`
 * so the canonical `CAP:DENY:<sid>:app_exec:<resource>` audit marker
 * already emitted by the kernel leg of `os_process_spawn` is paired
 * with a non-zero `$?` in the script (#493 "deny" done-when bullet).
 */
sosh_external_result_t sosh_try_exec_external(const char *command,
                                              const char *args,
                                              sosh_path_probe_fn probe,
                                              void *probe_ctx,
                                              sosh_spawn_fn spawn,
                                              void *spawn_ctx,
                                              int *out_exit);

#endif /* SOSH_EXEC_EXTERNAL_H */
