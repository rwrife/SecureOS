/**
 * @file session_manager_subject_for_session_test.c
 * @brief Host-side unit tests for `session_manager_subject_for_session`
 *        added in kernel/core/session_manager.c for the HAL call-site
 *        migration (issue #375, follow-up to #349 / PR #365).
 *
 * Cases:
 *   1. Out-of-range session_id        -> returns -1 (miss); out param
 *                                        sentinel preserved.
 *   2. In-range but unused slot       -> returns -1; sentinel preserved.
 *   3. In-use slot with known subject -> returns 0 + correct subject id.
 *   4. NULL out_subject               -> still distinguishes hit / miss
 *                                        without crashing.
 *   5. Round-trip via the
 *      `session_manager_first_session_for_subject` inverse predicate
 *      from #350 -- proves both accessors agree on the
 *      session<->subject relation.
 *
 * Launched by:
 *   build/scripts/test_session_manager_subject_for_session.sh
 *   (registered with build/scripts/test.sh).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/capability.h"
#include "../kernel/core/console.h"
#include "../kernel/core/ctx_switch.h"
#include "../kernel/core/session_manager.h"

/* ------------------------------------------------------------------ */
/* Host stubs (mirror tests/session_manager_first_for_subject_test.c) */
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

#define SENTINEL_SUBJ ((cap_subject_id_t)0xCAFEBABEu)

static void fail(const char *reason) {
  printf("TEST:FAIL:session_manager_subject_for_session:%s\n", reason);
  exit(1);
}

static int g_bootstrap_seeded = 0;

static void reset_session_table(void) {
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

static void test_out_of_range_session_id(void) {
  reset_session_table();
  cap_subject_id_t got = SENTINEL_SUBJ;
  /* SESSION_MAX is an internal enum (=8); use a value well past any
   * plausible bound so the predicate's range check fires. */
  int rc = session_manager_subject_for_session(99u, &got);
  if (rc != -1) fail("out_of_range:expected_miss");
  if (got != SENTINEL_SUBJ) fail("out_of_range:out_param_clobbered");
  printf("TEST:PASS:session_manager_subject_for_session:out_of_range\n");
}

static void test_unused_slot(void) {
  reset_session_table();
  /* Seed slot 0 stays in use (bootstrap); slot 1+ are free after
   * reset_session_table()'s destroy loop. */
  cap_subject_id_t got = SENTINEL_SUBJ;
  int rc = session_manager_subject_for_session(3u, &got);
  if (rc != -1) fail("unused:expected_miss");
  if (got != SENTINEL_SUBJ) fail("unused:out_param_clobbered_on_miss");
  printf("TEST:PASS:session_manager_subject_for_session:unused_slot\n");
}

static void test_in_use_slot(void) {
  reset_session_table();
  unsigned int created = 0u;
  if (!session_manager_create((cap_subject_id_t)17u, &created))
    fail("in_use:create_failed");

  cap_subject_id_t got = SENTINEL_SUBJ;
  if (session_manager_subject_for_session(created, &got) != 0)
    fail("in_use:expected_hit");
  if (got != (cap_subject_id_t)17u)
    fail("in_use:wrong_subject");
  printf("TEST:PASS:session_manager_subject_for_session:in_use_slot\n");
}

static void test_null_out_param(void) {
  reset_session_table();
  unsigned int created = 0u;
  if (!session_manager_create((cap_subject_id_t)23u, &created))
    fail("null:create_failed");

  if (session_manager_subject_for_session(created, NULL) != 0)
    fail("null:expected_hit_with_null_out");
  if (session_manager_subject_for_session(99u, NULL) != -1)
    fail("null:expected_miss_with_null_out");
  printf("TEST:PASS:session_manager_subject_for_session:null_out_param\n");
}

static void test_roundtrip_with_first_for_subject(void) {
  reset_session_table();
  unsigned int created = 0u;
  if (!session_manager_create((cap_subject_id_t)31u, &created))
    fail("roundtrip:create_failed");

  /* Forward: session id -> subject. */
  cap_subject_id_t subj = SENTINEL_SUBJ;
  if (session_manager_subject_for_session(created, &subj) != 0)
    fail("roundtrip:forward_miss");
  if (subj != (cap_subject_id_t)31u) fail("roundtrip:wrong_subject");

  /* Inverse: subject -> session id (#350 predicate). */
  unsigned int sid_back = 0xDEADBEEFu;
  if (session_manager_first_session_for_subject(subj, &sid_back) != 0)
    fail("roundtrip:inverse_miss");
  if (sid_back != created) fail("roundtrip:inverse_wrong_sid");

  /* After destroy, both predicates must miss for this session. */
  session_manager_destroy(created);
  if (session_manager_subject_for_session(created, NULL) != -1)
    fail("roundtrip:forward_not_miss_after_destroy");
  if (session_manager_first_session_for_subject((cap_subject_id_t)31u, NULL)
      != -1)
    fail("roundtrip:inverse_not_miss_after_destroy");

  printf("TEST:PASS:session_manager_subject_for_session:roundtrip\n");
}

int main(void) {
  printf("TEST:START:session_manager_subject_for_session\n");
  test_out_of_range_session_id();
  test_unused_slot();
  test_in_use_slot();
  test_null_out_param();
  test_roundtrip_with_first_for_subject();
  printf("TEST:PASS:session_manager_subject_for_session\n");
  return 0;
}
