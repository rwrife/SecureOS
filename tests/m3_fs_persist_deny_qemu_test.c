/**
 * @file m3_fs_persist_deny_qemu_test.c
 * @brief M3-on-M1 substrate peer of `tests/fs_service_persist_deny_test.c`
 *        (slice 3 of plan #277, issue #280).
 *
 * Spawns HelloApp via `launcher_fs_spawn_app_with_fs_caps(..., grant_write=0)`
 * so only the CAP_FS_READ handle is minted; the CAP_FS_WRITE slot in
 * `ipc_scratch` stays `CAP_HANDLE_NULL`. `helloapp_entry_fs_demo()`
 * then calls `ipc_send_h(write_handle, fs_write_port, ...)` with that
 * null handle, which must:
 *
 *   - return `IPC_ERR_CAP_DENIED`,
 *   - emit exactly one canonical `CAP:DENY:<actor>:fs_write:-` marker
 *     line through the shared formatter
 *     (`kernel/cap/cap_deny_marker.c`, post-#265),
 *   - NOT stage an envelope on the fs write port,
 *   - NOT spuriously wake any blocked process on that port.
 *
 * The read leg of the demo is left to run; with the read handle still
 * live it must return `IPC_OK` (an allow-side regression on the read
 * path would surface here as well).
 *
 * The deny-marker shape is validated through `cap_deny_marker_validate`
 * (#221) the same way `tests/m2_helloapp_deny_qemu_test.c` does, with
 * a substring assertion on `:fs_write:` to pin the capability name.
 *
 * Output markers (consumed by build/scripts/test_m3_fs_persist_deny_qemu.sh):
 *   TEST:PASS:m3_fs_persist_deny_qemu_no_stage
 *   TEST:PASS:m3_fs_persist_deny_marker_qemu
 *   TEST:PASS:m3_fs_persist_deny_qemu
 *
 * Issue: #280. Plan: plans/2026-05-24-m3-fs-on-m1-substrate.md slice 3.
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
#include "../kernel/proc/proc_sched.h"
#include "../kernel/svc/fs_svc.h"
#include "../kernel/user/helloapp.h"
#include "../kernel/user/launcher.h"
#include "../kernel/user/launcher_fs.h"
#include "harness/svc_subjects.h"

static int g_fail = 0;
static char g_capture[8192];

static void fail(const char *reason) {
  printf("TEST:FAIL:m3_fs_persist_deny_qemu:%s\n", reason);
  g_fail = 1;
}

/* Stdout-capture shim modeled on tests/m2_helloapp_deny_qemu_test.c.
 * We need to capture stdout across the demo call so the IPC-emitted
 * deny marker can be fed to `cap_deny_marker_validate`. */
typedef void (*capture_fn)(void);

static long capture_stdout(capture_fn fn) {
  fflush(stdout);
  FILE *tmp = tmpfile();
  if (!tmp) return -1;
  int saved_fd = dup(fileno(stdout));
  if (saved_fd < 0) { fclose(tmp); return -1; }
  if (dup2(fileno(tmp), fileno(stdout)) < 0) {
    close(saved_fd); fclose(tmp); return -1;
  }
  fn();
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
  proc_sched_reset();
  fs_svc_reset();
  ipc_port_table_reset();
  launcher_fs_reset();
  launcher_spawn_reset();
}

/* Captured-call context. */
static const address_space_t *g_run_aspace;
static ipc_port_t              g_run_read_port;
static ipc_port_t              g_run_write_port;
static helloapp_fs_demo_result_t g_run_demo;

static void do_demo(void) {
  helloapp_entry_fs_demo(g_run_aspace, g_run_read_port, g_run_write_port,
                         &g_run_demo);
}

/* Locate the first canonical `CAP:DENY:` line in `haystack`, copy it
 * (including the terminating '\n') into `out`, and return the number
 * of bytes copied. Returns 0 when no marker is found. The validator
 * requires the line to end in exactly one '\n'. */
static size_t extract_first_deny_line(const char *haystack,
                                      char *out,
                                      size_t out_size) {
  const char *start = strstr(haystack, "CAP:DENY:");
  if (start == NULL) {
    return 0;
  }
  const char *nl = strchr(start, '\n');
  if (nl == NULL) {
    return 0;
  }
  size_t len = (size_t)(nl - start) + 1u; /* include '\n' */
  if (len + 1u > out_size) {
    return 0;
  }
  memcpy(out, start, len);
  out[len] = '\0';
  return len;
}

int main(void) {
  reset_world();

  if (fs_svc_init() != FS_SVC_OK) {
    fail("fs_svc_init_failed");
    goto out;
  }
  ipc_port_t fs_read_port  = fs_svc_port_read();
  ipc_port_t fs_write_port = fs_svc_port_write();
  if (fs_read_port == IPC_PORT_INVALID || fs_write_port == IPC_PORT_INVALID) {
    fail("fs_ports_invalid");
    goto out;
  }

  /* Register the app PERSISTENT but never grant launcher_fs_write.
   * This isn't strictly required for the IPC-side deny (which fires
   * on the null write handle before any fan-out), but it matches the
   * "fail_closed" branch of the existing host fixture and keeps the
   * faux-fs state coherent in case future slices add a fan-out hop. */
  const cap_subject_id_t app = (cap_subject_id_t)SUBJECT_M2_HELLOAPP;
  if (launcher_fs_register_app(app, LAUNCHER_FS_MODE_PERSISTENT)
        != LAUNCHER_FS_OK) {
    fail("launcher_fs_register_persistent");
    goto out;
  }

  /* Spawn WITHOUT write — read handle minted, write slot CAP_HANDLE_NULL. */
  launcher_manifest_t m = {
      .subject_id       = app,
      .auto_grant_caps  = NULL,
      .auto_grant_count = 0u,
  };
  launcher_fs_spawn_t sp;
  if (launcher_fs_spawn_app_with_fs_caps(&m, 0, &sp) != LAUNCHER_OK) {
    fail("launcher_fs_spawn_failed");
    goto out;
  }
  if (sp.pid == PID_INVALID || !process_is_live_for_tests(sp.pid)) {
    fail("spawned_pcb_not_live");
    goto out;
  }
  if (sp.read_handle == CAP_HANDLE_NULL) {
    fail("spawned_read_handle_null");
    goto out;
  }
  if (sp.write_handle != CAP_HANDLE_NULL) {
    fail("spawned_write_handle_should_be_null");
    goto out;
  }

  /* Capture stdout across the demo so the marker can be validated. */
  g_run_aspace     = sp.aspace;
  g_run_read_port  = fs_read_port;
  g_run_write_port = fs_write_port;
  memset(&g_run_demo, 0, sizeof(g_run_demo));
  long n = capture_stdout(do_demo);
  if (n < 0) {
    fail("capture_stdout_failed");
    goto out;
  }

  if (g_run_demo.write_send_result != IPC_ERR_CAP_DENIED) {
    fail("write_send_did_not_deny");
    goto out;
  }

  /* The read leg must still succeed end-to-end through ipc_send_h
   * (it stages an envelope on the read port). A regression where a
   * deny on the write leg short-circuits the read leg would surface
   * here. */
  if (g_run_demo.read_send_result != IPC_OK) {
    fail("read_send_should_succeed");
    goto out;
  }

  /* Deny path MUST NOT stage on the write port — peek the slot. */
  {
    ipc_msg_v0 rx = {0};
    ipc_result_t peek = ipc_port_consume(fs_write_port, &rx);
    if (peek == IPC_OK) {
      fail("deny_path_staged_envelope");
      goto out;
    }
  }
  /* And no spurious wakes: scheduler must report no blocked tasks
   * (we never spun the scheduler, but this confirms the IPC layer
   * didn't wedge a phantom blocker either). */
  if (proc_sched_blocked_count_for_tests() != 0u) {
    fail("blocked_count_nonzero_after_deny");
    goto out;
  }
  printf("TEST:PASS:m3_fs_persist_deny_qemu_no_stage\n");

  /* Validate the canonical CAP:DENY marker shape. */
  char deny_line[256] = {0};
  size_t dlen = extract_first_deny_line(g_capture, deny_line,
                                        sizeof(deny_line));
  if (dlen == 0u) {
    printf("TEST:FAIL:m3_fs_persist_deny_qemu:no_deny_marker_emitted:captured=%s\n",
           g_capture);
    g_fail = 1;
    goto out;
  }
  char reason[64] = {0};
  int vrc = cap_deny_marker_validate(deny_line, reason, sizeof(reason));
  if (vrc != 0) {
    printf("TEST:FAIL:m3_fs_persist_deny_qemu:deny_marker_invalid:%s\n", reason);
    printf("TEST:FAIL:m3_fs_persist_deny_qemu:deny_line=%s", deny_line);
    g_fail = 1;
    goto out;
  }
  if (strstr(deny_line, ":fs_write:") == NULL) {
    printf("TEST:FAIL:m3_fs_persist_deny_qemu:wrong_cap_in_marker:deny_line=%s",
           deny_line);
    g_fail = 1;
    goto out;
  }
  printf("TEST:PASS:m3_fs_persist_deny_marker_qemu\n");

  if (launcher_fs_spawn_destroy(sp.pid) != LAUNCHER_OK) {
    fail("launcher_fs_spawn_destroy_failed");
    goto out;
  }

out:
  if (g_fail) {
    return 1;
  }
  printf("TEST:PASS:m3_fs_persist_deny_qemu\n");
  return 0;
}
