/**
 * @file session_manager_stub.c
 * @brief Host-test stub providing the two session_manager symbols
 *        broker_svc.c links against. See session_manager_stub.h.
 *
 * Issue: #350. Plan: plans/2026-05-26-m5-wm-cascade-on-substrate.md
 * slice 005b.
 */

#include "session_manager_stub.h"

#include "../../kernel/core/session_manager.h"

typedef struct {
  int               in_use;
  cap_subject_id_t  subject;
} sm_stub_row_t;

static sm_stub_row_t g_rows[SM_STUB_MAX];
static unsigned int  g_destroy_count = 0u;

void sm_stub_reset(void) {
  for (unsigned i = 0u; i < SM_STUB_MAX; ++i) {
    g_rows[i].in_use  = 0;
    g_rows[i].subject = (cap_subject_id_t)0u;
  }
  g_destroy_count = 0u;
}

int sm_stub_inject(cap_subject_id_t subject, unsigned int *out_sid) {
  for (unsigned i = 0u; i < SM_STUB_MAX; ++i) {
    if (!g_rows[i].in_use) {
      g_rows[i].in_use  = 1;
      g_rows[i].subject = subject;
      if (out_sid != 0) {
        *out_sid = i;
      }
      return 0;
    }
  }
  return -1;
}

int sm_stub_in_use(unsigned int sid) {
  if (sid >= SM_STUB_MAX) {
    return 0;
  }
  return g_rows[sid].in_use ? 1 : 0;
}

unsigned int sm_stub_destroy_count(void) {
  return g_destroy_count;
}

/* -- session_manager.h symbols broker_svc.c links against -- */

int session_manager_first_session_for_subject(cap_subject_id_t subject,
                                              unsigned int *out_session_id) {
  for (unsigned i = 0u; i < SM_STUB_MAX; ++i) {
    if (!g_rows[i].in_use) {
      continue;
    }
    if (g_rows[i].subject != subject) {
      continue;
    }
    if (out_session_id != 0) {
      *out_session_id = i;
    }
    return 0;
  }
  return -1;
}

void session_manager_destroy(unsigned int session_id) {
  if (session_id >= SM_STUB_MAX) {
    return;
  }
  if (!g_rows[session_id].in_use) {
    return;
  }
  g_rows[session_id].in_use  = 0;
  g_rows[session_id].subject = (cap_subject_id_t)0u;
  g_destroy_count = g_destroy_count + 1u;
}
