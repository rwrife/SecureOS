/**
 * @file m2_helloapp_deny_qemu_test.c
 * @brief Deny-path acceptance for slice 3 of plan #263 / issue #270.
 *
 * Counterpart to `m2_helloapp_allow_qemu_test.c`. Spawns HelloApp with
 * a manifest that does NOT auto-grant `CAP_CONSOLE_WRITE`, then calls
 * `helloapp_run_once()` and asserts:
 *
 *   - `helloapp_run_once` returns `IPC_ERR_CAP_DENIED`.
 *   - Exactly one canonical `CAP:DENY:<subject>:console_write:-` line
 *     was emitted to stdout, and it passes `cap_deny_marker_validate()`
 *     (the same gate used by `tests/syscall_entry_stub_test.c` and
 *     `tests/cap_deny_marker_shape_test.c` per #221 / PR #244).
 *   - The console-svc port's slot is still empty afterwards
 *     (deny path MUST NOT stage an envelope).
 *
 * The deny vocabulary is owned by `kernel/cap/cap_deny_marker.{c,h}` —
 * we never format the marker locally. The grammar is:
 *   `CAP:DENY:<actor_subject>:<capability_name>:<resource>\n`
 * with `<resource>` `-` when no resource id applies (the IPC layer's
 * default per `ipc_emit_deny_marker`).
 *
 * Output markers (consumed by build/scripts/test_m2_helloapp_deny_qemu.sh):
 *   TEST:PASS:helloapp_denied_console_write
 *   TEST:PASS:helloapp_deny
 *
 * Issue: #270. Plan: plans/2026-05-23-m2-on-m1-substrate.md slice 3.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_handle.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/ipc/ipc_msg.h"
#include "../kernel/ipc/ipc_ops.h"
#include "../kernel/ipc/ipc_port.h"
#include "../kernel/proc/process.h"
#include "../kernel/svc/console_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "harness/m2_subjects.h"

static int g_fail = 0;
static char g_capture[4096];

static void fail(const char *reason) {
  printf("TEST:FAIL:helloapp_deny:%s\n", reason);
  g_fail = 1;
}

/* Same stdout-capture shim as tests/syscall_entry_stub_test.c. We
 * intentionally duplicate the helper (~25 LoC) instead of factoring
 * it out per the plan's "helpers stay near the test that needs them"
 * convention; the substrate plan budgets 120 LoC per substrate test
 * helper and we are well inside that. */
typedef ipc_result_t (*capture_fn)(void);
static ipc_result_t g_capture_result;

static long capture_stdout(capture_fn fn) {
  fflush(stdout);
  FILE *tmp = tmpfile();
  if (!tmp) return -1;
  int saved_fd = dup(fileno(stdout));
  if (saved_fd < 0) { fclose(tmp); return -1; }
  if (dup2(fileno(tmp), fileno(stdout)) < 0) {
    close(saved_fd); fclose(tmp); return -1;
  }
  g_capture_result = fn();
  fflush(stdout);
  dup2(saved_fd, fileno(stdout));
  close(saved_fd);
  fseek(tmp, 0L, SEEK_SET);
  size_t n = fread(g_capture, 1u, sizeof(g_capture) - 1u, tmp);
  g_capture[n] = '\0';
  fclose(tmp);
  return (long)n;
}

static void reset_world(void) {
  launcher_reset();
  cap_handle_table_reset();
  cap_table_reset();
  process_table_reset();
  console_svc_reset();
  ipc_port_table_reset();
  launcher_spawn_reset();
}

/* Captured-call context. */
static const address_space_t *g_run_aspace;
static ipc_port_t              g_run_port;

static ipc_result_t do_run(void) {
  return helloapp_run_once(g_run_aspace, g_run_port);
}

int main(void) {
  reset_world();

  if (console_svc_init() != CONSOLE_SVC_OK) {
    fail("console_svc_init_failed");
    goto out;
  }
  ipc_port_t console_port = console_svc_port();
  if (console_port == IPC_PORT_INVALID) {
    fail("console_port_invalid");
    goto out;
  }

  /* Manifest WITHOUT auto-grant: the launcher still partitions the
   * aspace, creates the PCB, and writes the (NULL) handle into
   * `ipc_scratch[0..3]`. The slice-2 contract (#269) is that a no-grant
   * manifest leaves the scratch slot zeroed, which means HelloApp
   * decodes `cap_handle == 0` (CAP_HANDLE_NULL) and `ipc_send_h` takes
   * the canonical handle-deny path: stale-handle → owner == 0 →
   * `cap_check(0, CAP_IPC_SEND)` deny → marker emit. */
  launcher_manifest_t m = {
      .subject_id       = (cap_subject_id_t)SUBJECT_M2_HELLOAPP,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_spawn_t sp;
  if (launcher_spawn_app_from_manifest(&m, &sp) != LAUNCHER_OK) {
    fail("launcher_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }
  if (sp.granted_handle != CAP_HANDLE_NULL) {
    fail("no_grant_manifest_still_minted_handle");
    goto out;
  }

  /* Capture stdout across the single run so we can validate the deny
   * marker shape via cap_deny_marker_validate without parsing the rest
   * of the log. */
  g_run_aspace = sp.aspace;
  g_run_port   = console_port;
  long n = capture_stdout(do_run);
  if (n < 0) {
    fail("capture_stdout_failed");
    goto out;
  }

  if (g_capture_result != IPC_ERR_CAP_DENIED) {
    fail("helloapp_run_did_not_deny");
    goto out;
  }

  /* Validate the canonical marker. cap_deny_marker_validate consumes a
   * single-line marker; the IPC deny path emits exactly one such line
   * (see ipc_emit_deny_marker in kernel/ipc/ipc_ops.c). If extra
   * output appeared, treat it as a regression. */
  char reason[64] = {0};
  int vrc = cap_deny_marker_validate(g_capture, reason, sizeof(reason));
  if (vrc != 0) {
    /* Re-echo the captured bytes so failures are debuggable; strip
     * trailing newline noise from the diagnostic. */
    printf("TEST:FAIL:helloapp_deny:deny_marker_invalid:%s\n", reason);
    printf("TEST:FAIL:helloapp_deny:captured=%s\n", g_capture);
    g_fail = 1;
    goto out;
  }

  /* And confirm the capability the marker reports is `console_write`,
   * not some other CAP_IPC_* the recv side might emit on a wrong-owner
   * path. The marker grammar is `CAP:DENY:<subj>:<cap>:<res>` so a
   * substring search is sufficient and robust to subject-id renumbering. */
  if (strstr(g_capture, ":console_write:") == NULL) {
    printf("TEST:FAIL:helloapp_deny:wrong_cap_in_marker:captured=%s\n",
           g_capture);
    g_fail = 1;
    goto out;
  }

  /* Slot must still be empty: deny path MUST NOT stage. */
  ipc_msg_v0 rx = {0};
  ipc_result_t peek = ipc_port_consume(console_port, &rx);
  if (peek == IPC_OK) {
    fail("deny_path_staged_envelope");
    goto out;
  }

  printf("TEST:PASS:helloapp_denied_console_write\n");

  if (launcher_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:helloapp_deny\n");
  return 0;
}
