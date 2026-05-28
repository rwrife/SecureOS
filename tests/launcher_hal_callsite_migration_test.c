/**
 * @file launcher_hal_callsite_migration_test.c
 * @brief Host-side migration test for issue #375: subject-scoped HAL
 *        wrappers wired to the launcher/console call sites.
 *
 * What this pins:
 *   1. `session_manager_subject_for_session()` is the single source of
 *      truth used by `kernel/user/launcher_exec.c::app_native_input_read_char`
 *      to resolve the calling subject for the bare-metal fallback path.
 *      The test drives the same flow the launcher does (create a
 *      session, look up its subject, hand the subject to the wrapper).
 *   2. On allow, `input_hal_try_read_char_as` and `video_hal_write_as`
 *      invoke their backend primitive exactly once and propagate
 *      data/availability faithfully -- matching the existing console
 *      behaviour preserved in `kernel/core/console.c::console_write` and
 *      `console_run_command_loop` / `console_wait_for_yes_no`.
 *   3. On deny, the backends MUST NOT be invoked (this is the
 *      deny-by-default invariant: the previous, ungated call sites
 *      always invoked the backend, so the migration would silently
 *      regress without this check).
 *   4. When the session lookup misses (e.g. zombie session id), the
 *      input wrapper short-circuits like the legacy "no data" branch
 *      so the console can idle/retry instead of UB-faulting.
 *
 * Stubbed dependencies:
 *   - HAL backend primitives (video_hal_write, input_hal_try_read_char,
 *     mouse_hal_poll_event) so we don't pull the x86 PS/2 / VGA drivers
 *     into the host build. Invocation counts make the deny-by-default
 *     short-circuit assertable.
 *   - The session_manager.c dependencies (sched_*, ctx_*, console_*,
 *     vfb_font_draw_char, kmalloc/kfree, serial_hal_write) -- same
 *     pattern as tests/session_manager_subject_for_session_test.c.
 *
 * Launched by:
 *   build/scripts/test_launcher_hal_callsite_migration.sh, dispatched
 *   via build/scripts/test.sh.
 *
 * Issue: #375 (follow-up to PR #365 / #349).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/cap_gate.h"
#include "../kernel/cap/cap_table.h"
#include "../kernel/cap/capability.h"
#include "../kernel/core/console.h"
#include "../kernel/core/ctx_switch.h"
#include "../kernel/core/session_manager.h"
#include "../kernel/hal/hal_cap_entry.h"
#include "../kernel/hal/mouse_hal.h"

#define DENY_BUF_BYTES 128u

/* ------------------------------------------------------------------ */
/* Backend stubs                                                      */
/* ------------------------------------------------------------------ */
static int g_video_write_calls;
static int g_input_read_calls;
static int g_mouse_poll_calls;
static char g_last_video_message[64];
static int g_input_read_yields_char; /* 0 = empty, 1 = yield 'Q' */

void video_hal_write(const char *message) {
  g_video_write_calls++;
  if (message != NULL) {
    size_t len = strlen(message);
    if (len >= sizeof(g_last_video_message)) {
      len = sizeof(g_last_video_message) - 1u;
    }
    memcpy(g_last_video_message, message, len);
    g_last_video_message[len] = '\0';
  }
}

int input_hal_try_read_char(char *out_char) {
  g_input_read_calls++;
  if (g_input_read_yields_char && out_char != NULL) {
    *out_char = 'Q';
    return 1;
  }
  return 0;
}

int mouse_hal_poll_event(mouse_event_t *out_event) {
  g_mouse_poll_calls++;
  (void)out_event;
  return 0;
}

/* ------------------------------------------------------------------ */
/* session_manager.c host shims (mirror                                */
/* tests/session_manager_subject_for_session_test.c)                  */
/* ------------------------------------------------------------------ */
void serial_hal_write(const char *s) { (void)s; }
void *kmalloc(unsigned long n) { (void)n; return NULL; }
void kfree(void *p) { (void)p; }

int sched_spawn(const char *name, void (*entry)(void *), void *arg) {
  (void)name; (void)entry; (void)arg;
  return 0;
}
void sched_run_forever(void) { }

int ctx_save(ctx_jmp_buf_t *buf) { (void)buf; return 0; }
void ctx_resume(ctx_jmp_buf_t *buf, int value) {
  (void)buf; (void)value;
  exit(99);
}
int ctx_resumes(ctx_jmp_buf_t *buf) { (void)buf; return 0; }
void ctx_call_on_stack(void *stack_top, void (*func)(void)) {
  (void)stack_top; (void)func;
}

void console_init(console_context_t *context, cap_subject_id_t subject_id) {
  if (context) {
    memset(context, 0, sizeof(*context));
    context->subject_id = subject_id;
  }
}
void console_bind_context(console_context_t *context) { (void)context; }
void console_run(void) { }
void console_process_injected(void) { }
int console_try_read_injected(char *out_char) { (void)out_char; return 0; }
void console_write(const char *s) { (void)s; }
void console_idle_wait(void) { }

void vfb_font_draw_char(unsigned char *buf, unsigned int w, unsigned int h,
                        unsigned int x, unsigned int y, char ch,
                        unsigned char fg, unsigned char bg) {
  (void)buf; (void)w; (void)h; (void)x; (void)y;
  (void)ch; (void)fg; (void)bg;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static void die(const char *reason) {
  printf("TEST:FAIL:launcher_hal_callsite_migration:%s\n", reason);
  exit(1);
}

static const cap_subject_id_t LAUNCHER_ROOT_SUBJECT = 0u;
static const cap_subject_id_t LAUNCHER_SUBJECT = 1u;

static void bootstrap_admin(void) {
  /* Mirrors tests/win_gfx_callsite_test.c: only the root subject may
   * mint CAP_CAPABILITY_ADMIN; the launcher then mints leaf caps. */
  if (cap_grant_as_for_tests(LAUNCHER_ROOT_SUBJECT, LAUNCHER_SUBJECT,
                             CAP_CAPABILITY_ADMIN) != CAP_OK) {
    die("bootstrap_admin:grant_admin");
  }
}

static void assert_deny_marker(const char *label,
                               const char *buf,
                               capability_id_t cap_id) {
  char reason[64];
  reason[0] = '\0';
  if (cap_deny_marker_validate(buf, reason, sizeof(reason)) != 0) {
    fprintf(stderr, "%s: deny marker invalid: %s line=%s\n", label, reason,
            buf);
    die("deny_marker_invalid");
  }
  const char *cap_name = cap_deny_marker_name(cap_id);
  if (cap_name == NULL || strstr(buf, cap_name) == NULL) {
    die("deny_marker_missing_cap_name");
  }
}

/* Emulates kernel/user/launcher_exec.c::app_native_input_read_char's
 * non-WM fallback after migration (#375): resolve the session's subject,
 * route through the subject-scoped wrapper, treat deny as "no data". */
static int sim_launcher_input_read_char(unsigned int sid, char *out_char) {
  if (out_char == 0) return 3;
  cap_subject_id_t subject = 0;
  if (session_manager_subject_for_session(sid, &subject) != 0) {
    *out_char = '\0';
    return 2;
  }
  int got = 0;
  if (input_hal_try_read_char_as(subject, out_char, &got, 0, 0u) == CAP_OK
      && got) {
    return 0;
  }
  *out_char = '\0';
  return 2;
}

/* Emulates kernel/core/console.c::console_write's framebuffer leg after
 * migration (#375): silently drop on deny (preserves the existing
 * CAP_CONSOLE_WRITE deny posture). */
static void sim_console_write_hardware(cap_subject_id_t console_subject,
                                       const char *message) {
  (void)video_hal_write_as(console_subject, message, 0, 0u);
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_input_callsite_allow_via_session_lookup(void) {
  cap_reset_for_tests();
  bootstrap_admin();

  /* Create a fresh session bound to a subject. */
  unsigned int sid = 0u;
  cap_subject_id_t subject = (cap_subject_id_t)3u;
  if (!session_manager_create(subject, &sid)) die("input_allow:create");

  /* Grant the keyboard cap to the session's subject. */
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT, subject,
                             CAP_INPUT_KEYBOARD) != CAP_OK) {
    die("input_allow:grant_keyboard");
  }

  /* Simulate a pending PS/2 byte and drive the migrated call site. */
  g_input_read_yields_char = 1;
  int before = g_input_read_calls;
  char ch = '\0';
  int rc = sim_launcher_input_read_char(sid, &ch);
  if (rc != 0) die("input_allow:rc_not_zero");
  if (ch != 'Q') die("input_allow:char_not_propagated");
  if (g_input_read_calls != before + 1) die("input_allow:backend_not_called");

  session_manager_destroy(sid);
}

static void test_input_callsite_deny_short_circuits_backend(void) {
  cap_reset_for_tests();

  unsigned int sid = 0u;
  cap_subject_id_t subject = (cap_subject_id_t)4u;
  if (!session_manager_create(subject, &sid)) die("input_deny:create");

  /* Deliberately do NOT grant CAP_INPUT_KEYBOARD. */
  g_input_read_yields_char = 1; /* would yield a byte if reached */
  int before = g_input_read_calls;
  char ch = '\0';
  int rc = sim_launcher_input_read_char(sid, &ch);
  if (rc != 2) die("input_deny:rc_not_no_data");
  if (ch != '\0') die("input_deny:clobbered_out_char");
  if (g_input_read_calls != before) {
    die("input_deny:backend_invoked_despite_deny");
  }

  /* Audit ring must record the CHECK as CAP_ERR_MISSING. */
  int found = 0;
  size_t count = cap_audit_count_for_tests();
  for (size_t i = 0u; i < count; ++i) {
    cap_audit_event_t event = {0};
    if (cap_audit_get_for_tests(i, &event) != CAP_OK) continue;
    if (event.operation == CAP_AUDIT_OP_CHECK &&
        event.subject_id == subject &&
        event.capability_id == CAP_INPUT_KEYBOARD &&
        event.result == CAP_ERR_MISSING) {
      found = 1;
      break;
    }
  }
  if (!found) die("input_deny:no_audit_check_missing");

  session_manager_destroy(sid);
}

static void test_input_callsite_session_lookup_miss(void) {
  cap_reset_for_tests();
  /* No session created for sid 7 -> lookup misses -> "no data". */
  int before = g_input_read_calls;
  char ch = '\0';
  int rc = sim_launcher_input_read_char(7u, &ch);
  if (rc != 2) die("input_miss:rc_not_no_data");
  if (ch != '\0') die("input_miss:clobbered_out_char");
  if (g_input_read_calls != before) {
    die("input_miss:backend_invoked_on_session_miss");
  }
}

static void test_video_callsite_allow(void) {
  cap_reset_for_tests();
  bootstrap_admin();

  cap_subject_id_t subject = (cap_subject_id_t)5u;
  if (cap_grant_as_for_tests(LAUNCHER_SUBJECT, subject,
                             CAP_GFX_FRAMEBUFFER) != CAP_OK) {
    die("video_allow:grant_gfx");
  }

  int before = g_video_write_calls;
  sim_console_write_hardware(subject, "console-banner");
  if (g_video_write_calls != before + 1) die("video_allow:backend_not_called");
  if (strcmp(g_last_video_message, "console-banner") != 0) {
    die("video_allow:payload_corrupted");
  }
}

static void test_video_callsite_deny_drops_silently(void) {
  cap_reset_for_tests();

  cap_subject_id_t subject = (cap_subject_id_t)6u;
  /* No grant. */
  int before = g_video_write_calls;
  sim_console_write_hardware(subject, "should-be-dropped");
  if (g_video_write_calls != before) {
    die("video_deny:backend_invoked_despite_deny");
  }

  /* Deny marker buf path: also verify the wrapper emits a conformant
   * deny marker when the caller passes a buffer (the migrated call
   * sites do not pass one -- they drop silently -- but the contract
   * itself is exercised here so the shared cap-deny grammar regression
   * surface is covered). */
  char buf[DENY_BUF_BYTES];
  memset(buf, 0, sizeof(buf));
  cap_result_t r = video_hal_write_as(subject, "x", buf, sizeof(buf));
  if (r != CAP_ERR_MISSING) die("video_deny:wrapper_not_missing");
  assert_deny_marker("video", buf, CAP_GFX_FRAMEBUFFER);
}

int main(void) {
  printf("TEST:START:launcher_hal_callsite_migration\n");

  test_input_callsite_allow_via_session_lookup();
  printf("TEST:PASS:launcher_input_callsite_allow_via_session_lookup\n");

  test_input_callsite_deny_short_circuits_backend();
  printf("TEST:PASS:launcher_input_callsite_deny_short_circuits_backend\n");

  test_input_callsite_session_lookup_miss();
  printf("TEST:PASS:launcher_input_callsite_session_lookup_miss\n");

  test_video_callsite_allow();
  printf("TEST:PASS:console_video_callsite_allow\n");

  test_video_callsite_deny_drops_silently();
  printf("TEST:PASS:console_video_callsite_deny_drops_silently\n");

  printf("TEST:PASS:launcher_hal_callsite_migration\n");
  return 0;
}
