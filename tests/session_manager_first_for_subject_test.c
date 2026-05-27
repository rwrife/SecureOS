/**
 * @file session_manager_first_for_subject_test.c
 * @brief Host-side unit tests for the M5-SUBSTRATE-005a enumerator
 *        predicate `session_manager_first_session_for_subject`
 *        (issue #350, plan
 *        plans/2026-05-26-m5-wm-cascade-on-substrate.md).
 *
 * Cases:
 *   1. No sessions live  -> predicate returns -1 (miss), out param
 *      untouched (sentinel preserved).
 *   2. One session live  -> hit returns 0 + correct session id.
 *   3. Two sessions for same owner -> first call returns the
 *      lowest-indexed slot; after destroying it, the next call
 *      advances to the surviving slot; after destroying that one,
 *      the predicate misses (cascade-loop termination property).
 *   4. Two owners interleaved -> predicate returns only the matching
 *      owner's slot; the other owner's slot stays untouched.
 *   5. NULL out_session_id is accepted (hit returns 0, miss returns -1)
 *      so callers that only want a "does this owner own anything"
 *      boolean can pass NULL.
 *
 * Launched by:
 *   build/scripts/test_session_manager_first_for_subject.sh (registered
 *   with build/scripts/test.sh under the
 *   `session_manager_first_for_subject` target).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/core/session_manager.h"

/* ------------------------------------------------------------------ */
/* Host stubs                                                         */
/* ------------------------------------------------------------------ */
/* session_manager.c references a handful of kernel-only symbols. We
 * stub them out here so the host link succeeds. The predicate itself
 * only touches g_sessions[] state, so the stubs can be no-ops. */

#include "../kernel/core/console.h"
#include "../kernel/core/ctx_switch.h"

void serial_hal_write(const char *s) { (void)s; }
void *kmalloc(unsigned long n) { (void)n; return NULL; }
void kfree(void *p) { (void)p; }

int sched_spawn(const char *name, void (*entry)(void *), void *arg) {
  (void)name; (void)entry; (void)arg;
  return 0;
}
void sched_run_forever(void) { /* never called from these tests */ }

int ctx_save(ctx_jmp_buf_t *buf) { (void)buf; return 0; }
void ctx_resume(ctx_jmp_buf_t *buf, int value) {
  (void)buf; (void)value;
  /* Declared noreturn in the kernel header; we are never invoked from
   * these unit tests, but we must not return. */
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

/* vfb_font_draw_char is referenced by session_manager.c's text path. */
void vfb_font_draw_char(unsigned char *buf, unsigned int w, unsigned int h,
                        unsigned int x, unsigned int y, char ch,
                        unsigned char fg, unsigned char bg) {
  (void)buf; (void)w; (void)h; (void)x; (void)y;
  (void)ch; (void)fg; (void)bg;
}

/* ------------------------------------------------------------------ */
/* Test helpers                                                       */
/* ------------------------------------------------------------------ */

static void fail(const char *reason) {
  printf("TEST:FAIL:session_manager_first_for_subject:%s\n", reason);
  exit(1);
}

#define SENTINEL_SID 0xDEADBEEFu

/* Reset session table to a clean state by binding bootstrap subject 1
 * and then destroying any leftover sessions from prior tests. We avoid
 * session_manager_start (which calls sched_run_forever) and use
 * session_manager_create directly, but we still need an initial seeded
 * state; the simplest is to walk a destroy loop across all possible
 * session ids and then proceed (the destroy is a no-op on free slots).
 *
 * NOTE: session_manager_destroy refuses to destroy the *active*
 * session. The active id is initialized to 0 by C BSS zeroing and is
 * only advanced inside session_manager_start (which we don't call),
 * so as long as test sessions ids end up != 0, destroy works. We
 * therefore seed a dummy bootstrap session at slot 0 (it stays the
 * active session and is never enumerated against the test owners).
 */

static int g_bootstrap_seeded = 0;

static void reset_session_table(void) {
  /* The test never calls session_manager_start, so g_active_session_id
   * stays at its BSS value of 0. We seed slot 0 *once* with a sentinel
   * owner that no test uses; destroy() is a no-op on the active slot,
   * so slot 0 persists across tests. Then we drain slots 1..31 (free
   * slots no-op; in-use slots get torn down). */
  if (!g_bootstrap_seeded) {
    unsigned int seed = 0u;
    if (!session_manager_create((cap_subject_id_t)0xFFFFu, &seed)) {
      fail("seed_bootstrap_create");
    }
    if (seed != 0u) {
      fail("seed_bootstrap_not_slot0");
    }
    g_bootstrap_seeded = 1;
  }
  for (unsigned int sid = 1u; sid < 32u; ++sid) {
    session_manager_destroy(sid);
  }
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_no_sessions_for_owner(void) {
  reset_session_table();
  unsigned int sid = SENTINEL_SID;
  int rc = session_manager_first_session_for_subject((cap_subject_id_t)42, &sid);
  if (rc != -1) fail("no_sessions:expected_miss");
  if (sid != SENTINEL_SID) fail("no_sessions:out_param_clobbered_on_miss");
  printf("TEST:PASS:session_manager_first_for_subject:no_sessions\n");
}

static void test_single_owner_single_session(void) {
  reset_session_table();
  unsigned int created = 0u;
  if (!session_manager_create((cap_subject_id_t)7, &created)) fail("single:create");
  unsigned int found = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)7, &found) != 0)
    fail("single:expected_hit");
  if (found != created) fail("single:wrong_session_id");
  printf("TEST:PASS:session_manager_first_for_subject:single\n");
}

static void test_multiple_sessions_same_owner_drain(void) {
  reset_session_table();
  unsigned int a = 0u, b = 0u;
  if (!session_manager_create((cap_subject_id_t)3, &a)) fail("drain:create_a");
  if (!session_manager_create((cap_subject_id_t)3, &b)) fail("drain:create_b");
  if (a == b) fail("drain:duplicate_session_ids");

  /* First hit must be the lowest-indexed slot. */
  unsigned int first = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)3, &first) != 0)
    fail("drain:miss_first");
  unsigned int expected_first = (a < b) ? a : b;
  unsigned int expected_second = (a < b) ? b : a;
  if (first != expected_first) fail("drain:not_lowest_first");

  /* Destroy it, next call must advance to the surviving slot. */
  session_manager_destroy(first);
  unsigned int second = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)3, &second) != 0)
    fail("drain:miss_second");
  if (second != expected_second) fail("drain:not_advancing");

  /* Destroy the last one, predicate must now miss. */
  session_manager_destroy(second);
  unsigned int third = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)3, &third) != -1)
    fail("drain:expected_miss_after_drain");
  if (third != SENTINEL_SID) fail("drain:out_param_clobbered_after_drain");
  printf("TEST:PASS:session_manager_first_for_subject:drain\n");
}

static void test_owner_isolation(void) {
  reset_session_table();
  unsigned int s_a = 0u, s_b = 0u;
  if (!session_manager_create((cap_subject_id_t)11, &s_a)) fail("iso:create_a");
  if (!session_manager_create((cap_subject_id_t)22, &s_b)) fail("iso:create_b");

  unsigned int found_a = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)11, &found_a) != 0)
    fail("iso:miss_a");
  if (found_a != s_a) fail("iso:wrong_a_session");

  unsigned int found_b = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)22, &found_b) != 0)
    fail("iso:miss_b");
  if (found_b != s_b) fail("iso:wrong_b_session");

  /* Destroying owner 11's session must NOT affect owner 22. */
  session_manager_destroy(found_a);
  if (session_manager_first_session_for_subject((cap_subject_id_t)11, NULL) != -1)
    fail("iso:expected_miss_after_destroy_a");

  unsigned int b_after = SENTINEL_SID;
  if (session_manager_first_session_for_subject((cap_subject_id_t)22, &b_after) != 0)
    fail("iso:b_disappeared_with_a");
  if (b_after != s_b) fail("iso:b_moved");
  printf("TEST:PASS:session_manager_first_for_subject:owner_isolation\n");
}

static void test_null_out_param(void) {
  reset_session_table();
  unsigned int created = 0u;
  if (!session_manager_create((cap_subject_id_t)55, &created)) fail("null:create");

  /* Hit with NULL out param: must still return 0 and not crash. */
  if (session_manager_first_session_for_subject((cap_subject_id_t)55, NULL) != 0)
    fail("null:expected_hit_with_null_out");

  /* Miss with NULL out param: must still return -1 and not crash. */
  if (session_manager_first_session_for_subject((cap_subject_id_t)9999, NULL) != -1)
    fail("null:expected_miss_with_null_out");
  printf("TEST:PASS:session_manager_first_for_subject:null_out_param\n");
}

int main(void) {
  printf("TEST:START:session_manager_first_for_subject\n");
  test_no_sessions_for_owner();
  test_single_owner_single_session();
  test_multiple_sessions_same_owner_drain();
  test_owner_isolation();
  test_null_out_param();
  printf("TEST:PASS:session_manager_first_for_subject\n");
  return 0;
}
