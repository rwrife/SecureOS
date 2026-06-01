/**
 * @file sosh_exec_external.c
 * @brief sosh embedder helper: probe-then-spawn fall-through for external
 *        commands. See `sosh_exec_external.h` for the contract.
 *
 * #493 sub-slice of #410 (depends on #422). Closes the gap that today
 * makes `sosh> hello` silently no-op despite `/apps/hello` being a
 * staged SOF and the `os_process_spawn` syscall / `CAP_APP_EXEC` gate
 * already being wired (PR #427).
 */

#include "sosh_exec_external.h"

/* Small local strcat-into-fixed-buffer — no libc on the freestanding
 * sosh app build. Idiom mirrors `sosh_app_strcpy` in main.c. */
static void sosh_exec_strcat(char *dst, const char *src, int max) {
  int dlen = 0;
  int i = 0;
  if (src == 0) return;
  while (dst[dlen]) dlen++;
  while (src[i] && dlen + i < max - 1) {
    dst[dlen + i] = src[i];
    i++;
  }
  dst[dlen + i] = '\0';
}

static void sosh_exec_strcpy(char *dst, const char *src, int max) {
  int i = 0;
  if (src == 0) { dst[0] = '\0'; return; }
  while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

/*
 * Probe a single candidate. We treat OS_STATUS_OK as "resolved". The
 * production probe uses `os_fs_list_dir` (the lightest filesystem
 * touch that still confirms presence — same idiom as the `exists`
 * builtin). Test stubs may emulate.
 */
static int sosh_exec_probe_path(const char *path,
                                sosh_path_probe_fn probe,
                                void *probe_ctx) {
  if (probe == 0 || path == 0 || path[0] == '\0') return 0;
  return probe(path, probe_ctx) == OS_STATUS_OK;
}

sosh_external_result_t sosh_try_exec_external(const char *command,
                                              const char *args,
                                              sosh_path_probe_fn probe,
                                              void *probe_ctx,
                                              sosh_spawn_fn spawn,
                                              void *spawn_ctx,
                                              int *out_exit) {
  char resolved[256];
  int resolved_ok = 0;
  const char *argv[3];
  int child_exit = 0;
  os_status_t st;

  if (out_exit) *out_exit = 0;
  if (command == 0 || command[0] == '\0' || spawn == 0) {
    return SOSH_EXTERNAL_NOT_FOUND;
  }

  /*
   * Search order — documented in docs/abi/sosh-capability-contract.md
   * §4 (and now in #493): /apps/<cmd>, /apps/dev/<cmd>, then <cmd>
   * literal. This intentionally probes BEFORE the spawn so we never
   * pay a kernel round-trip for an obvious miss, and so unknown-
   * command behaviour stays cheap.
   */
  if (command[0] == '/') {
    /* Absolute path — single probe, no /apps/ prefixing. */
    sosh_exec_strcpy(resolved, command, sizeof(resolved));
    resolved_ok = sosh_exec_probe_path(resolved, probe, probe_ctx);
  } else {
    sosh_exec_strcpy(resolved, "/apps/", sizeof(resolved));
    sosh_exec_strcat(resolved, command, sizeof(resolved));
    resolved_ok = sosh_exec_probe_path(resolved, probe, probe_ctx);

    if (!resolved_ok) {
      sosh_exec_strcpy(resolved, "/apps/dev/", sizeof(resolved));
      sosh_exec_strcat(resolved, command, sizeof(resolved));
      resolved_ok = sosh_exec_probe_path(resolved, probe, probe_ctx);
    }
    if (!resolved_ok) {
      sosh_exec_strcpy(resolved, command, sizeof(resolved));
      resolved_ok = sosh_exec_probe_path(resolved, probe, probe_ctx);
    }
  }

  if (!resolved_ok) {
    /*
     * No probe matched — let the caller emit its existing
     * "command not found" message + rc 127. We deliberately
     * return *out_exit = 0 here; the caller owns the unknown-
     * command rc shape.
     */
    return SOSH_EXTERNAL_NOT_FOUND;
  }

  /* Build argv[]: { resolved-or-command, args, NULL }. The wrapper
   * forwards argv[0] as the program name and space-joins argv[1..]
   * into the raw-args string the launcher expects. We pass the
   * original command name (not the probed path) as argv[0] so the
   * child sees the conventional self-name. */
  argv[0] = command;
  argv[1] = (args && args[0]) ? args : 0;
  argv[2] = 0;
  /* When args is NULL the wrapper sees a single-element argv and
   * joins nothing — exactly the no-args case. */
  if (argv[1] == 0) {
    argv[1] = 0;
  }

  st = spawn(resolved, argv, /*flags=*/0u, &child_exit, spawn_ctx);
  if (st == OS_STATUS_OK) {
    if (out_exit) *out_exit = child_exit;
    return SOSH_EXTERNAL_RAN;
  }
  if (st == OS_STATUS_DENIED) {
    /* Canonical CAP:DENY marker already on the wire from the kernel
     * leg of os_process_spawn — do NOT swallow it. Surface a non-
     * zero rc so the script's $? reflects the deny (#493 done-when). */
    if (out_exit) *out_exit = SOSH_EXEC_RC_DENIED;
    return SOSH_EXTERNAL_RAN;
  }
  if (st == OS_STATUS_NOT_FOUND) {
    /* Probe said the file existed but the launcher disagrees (raced
     * unlink, missing SOF header, etc). Treat as run-but-failed
     * rather than "not found at the sosh layer" — we already passed
     * the probe and the caller will print no "not found" line. */
    if (out_exit) *out_exit = SOSH_EXEC_RC_ERROR;
    return SOSH_EXTERNAL_RAN;
  }
  /* OS_STATUS_ERROR or any other future status. */
  if (out_exit) *out_exit = SOSH_EXEC_RC_ERROR;
  return SOSH_EXTERNAL_RAN;
}
