/**
 * @file cap_deny_marker_shape_test.c
 * @brief Conformance test for the canonical CAP:DENY:<...> serial marker.
 *
 * Issue #211. The marker grammar lives in
 * `docs/abi/capability-deny-contract.md` §4 and is implemented by
 * `kernel/cap/cap_deny_marker.{h,c}`.
 *
 * This test is the SINGLE PLACE that asserts marker shape. Future deny-path
 * services (M2 console #92 [done], M3 fs #108, M4 broker #115, M1 IPC #210)
 * register their (subject_id, capability_id, resource) triples with the
 * harness below — they MUST NOT invent independent grep regexes elsewhere.
 *
 * The test exercises:
 *   1. Formatter round-trips: format -> validate is a no-op.
 *   2. Driver coverage: every currently-implemented deny path's exemplar
 *      from the contract doc §7 round-trips.
 *   3. Negative cases: hand-crafted divergent shapes (extra field, missing
 *      newline, unknown cap name, embedded ':' in resource, trailing
 *      garbage) all reject with the expected reason code.
 *   4. Cap-name table coverage: every capability_id_t value from the enum
 *      has a registered marker name (no silent drift when the enum grows).
 *
 * Output markers (consumed by build/scripts/test_cap_deny_marker_shape.sh):
 *   TEST:PASS:cap_deny_marker_shape_format_roundtrip
 *   TEST:PASS:cap_deny_marker_shape_drivers
 *   TEST:PASS:cap_deny_marker_shape_negative
 *   TEST:PASS:cap_deny_marker_shape_cap_table
 *   TEST:PASS:cap_deny_marker_shape          (all phases passed)
 *
 * On any failure: `TEST:FAIL:cap_deny_marker_shape:<reason>` and exit(1).
 *
 * Pure host-side, no kernel runtime dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/cap/cap_deny_marker.h"
#include "../kernel/cap/capability.h"

static int g_fail = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:cap_deny_marker_shape:%s\n", reason);
  g_fail = 1;
}

/* ---- Phase 1: formatter round-trip ---- */
static void test_format_roundtrip(void) {
  char buf[CAP_DENY_MARKER_MAX];
  int n = cap_deny_marker_format(42u, CAP_CONSOLE_WRITE, "-", buf, sizeof(buf));
  if (n <= 0) { fail("format_roundtrip_format_failed"); return; }
  if (strcmp(buf, "CAP:DENY:42:console_write:-\n") != 0) {
    fail("format_roundtrip_unexpected_output");
    return;
  }
  if (cap_deny_marker_validate(buf, NULL, 0u) != 0) {
    fail("format_roundtrip_validate_failed");
    return;
  }
  printf("TEST:PASS:cap_deny_marker_shape_format_roundtrip\n");
}

/* ---- Phase 2: driver coverage (one entry per implemented or planned
 * deny path; the contract doc §7 examples are the authoritative drivers). */
typedef struct {
  const char *label;            /* test label, also greppable */
  cap_subject_id_t actor;
  capability_id_t cap;
  const char *resource;
  const char *expected;         /* exact wire bytes (incl. trailing \n) */
} driver_t;

static const driver_t drivers[] = {
    /* §7.1 — M2 HelloApp console_write deny (#92, shipped). */
    {"console_write_deny",      42u, CAP_CONSOLE_WRITE,    "-",
     "CAP:DENY:42:console_write:-\n"},
    /* §7.2 — M3 fs_read deny (#108). */
    {"fs_read_deny",            42u, CAP_FS_READ,          "/os/notes.txt",
     "CAP:DENY:42:fs_read:/os/notes.txt\n"},
    /* §7.3 — M4 broker share deny (#115). */
    {"broker_share_deny",       42u, CAP_CAPABILITY_ADMIN,
     "broker_share=cap=fs_read,target=17",
     "CAP:DENY:42:capability_admin:broker_share=cap=fs_read,target=17\n"},
    /* M1 IPC send deny (#210, shipped). The IPC path now routes
     * through cap_deny_marker_format directly (was a bespoke printf)
     * so its on-wire shape is asserted here against the canonical
     * formatter. */
    {"ipc_send_deny",           7u,  CAP_IPC_SEND,
     "ipc_port=3",
     "CAP:DENY:7:ipc_send:ipc_port=3\n"},
    /* M1 IPC recv deny (#210, shipped). Same path, mirror cap. */
    {"ipc_recv_deny",           7u,  CAP_IPC_RECV,
     "-",
     "CAP:DENY:7:ipc_recv:-\n"},
    /* Capability audit_event_subscribe deny (per §5 async fallback). */
    {"event_subscribe_deny",    9u,  CAP_EVENT_SUBSCRIBE,  "topic=fs.changed",
     "CAP:DENY:9:event_subscribe:topic=fs.changed\n"},
    /* Network deny (URL scheme gate, #79). */
    {"network_deny",            5u,  CAP_NETWORK,          "https//example.com",
     "CAP:DENY:5:network:https//example.com\n"},
};

static void test_drivers(void) {
  const size_t n = sizeof(drivers) / sizeof(drivers[0]);
  for (size_t i = 0u; i < n; ++i) {
    char buf[CAP_DENY_MARKER_MAX];
    int rc = cap_deny_marker_format(drivers[i].actor, drivers[i].cap,
                                    drivers[i].resource, buf, sizeof(buf));
    if (rc <= 0) {
      char r[64];
      snprintf(r, sizeof(r), "driver_format_failed:%s", drivers[i].label);
      fail(r);
      return;
    }
    if (strcmp(buf, drivers[i].expected) != 0) {
      char r[128];
      snprintf(r, sizeof(r), "driver_mismatch:%s", drivers[i].label);
      fail(r);
      printf("  expected: %sgot:      %s", drivers[i].expected, buf);
      return;
    }
    char reason[64];
    if (cap_deny_marker_validate(buf, reason, sizeof(reason)) != 0) {
      char r[128];
      snprintf(r, sizeof(r), "driver_validate_failed:%s:%s",
               drivers[i].label, reason);
      fail(r);
      return;
    }
  }
  printf("TEST:PASS:cap_deny_marker_shape_drivers\n");
}

/* ---- Phase 3: negative cases ---- */
typedef struct {
  const char *label;
  const char *line;
  const char *expected_reason;
} neg_t;

static const neg_t negatives[] = {
    {"missing_prefix",       "FOO:DENY:42:console_write:-\n",         "missing_prefix"},
    {"missing_newline",      "CAP:DENY:42:console_write:-",           "missing_newline"},
    {"too_few_fields",       "CAP:DENY:42:console_write\n",           "field_count"},
    {"too_many_fields",      "CAP:DENY:42:console_write:-:extra\n",   "field_count"},
    {"actor_non_decimal",    "CAP:DENY:abc:console_write:-\n",        "actor_not_decimal"},
    {"actor_empty",          "CAP:DENY::console_write:-\n",           "actor_not_decimal"},
    {"unknown_cap",          "CAP:DENY:42:bogus_cap:-\n",             "cap_unknown"},
    {"resource_empty",       "CAP:DENY:42:console_write:\n",          "resource_empty"},
    {"trailing_garbage",     "CAP:DENY:42:console_write:-\nextra\n",  "trailing_garbage"},
    {"embedded_newline_res", "CAP:DENY:42:console_write:foo\nbar\n",  "trailing_garbage"},
};

static void test_negatives(void) {
  const size_t n = sizeof(negatives) / sizeof(negatives[0]);
  for (size_t i = 0u; i < n; ++i) {
    char reason[64];
    int rc = cap_deny_marker_validate(negatives[i].line, reason, sizeof(reason));
    if (rc == 0) {
      char r[128];
      snprintf(r, sizeof(r), "negative_unexpectedly_passed:%s", negatives[i].label);
      fail(r);
      return;
    }
    if (strcmp(reason, negatives[i].expected_reason) != 0) {
      char r[160];
      snprintf(r, sizeof(r), "negative_wrong_reason:%s:got=%s:want=%s",
               negatives[i].label, reason, negatives[i].expected_reason);
      fail(r);
      return;
    }
  }
  /* Formatter must also refuse to PRODUCE bad shapes. */
  char buf[CAP_DENY_MARKER_MAX];
  if (cap_deny_marker_format(1u, CAP_CONSOLE_WRITE, "", buf, sizeof(buf)) >= 0) {
    fail("format_accepted_empty_resource"); return;
  }
  if (cap_deny_marker_format(1u, CAP_CONSOLE_WRITE, "a:b", buf, sizeof(buf)) >= 0) {
    fail("format_accepted_colon_in_resource"); return;
  }
  if (cap_deny_marker_format(1u, CAP_CONSOLE_WRITE, "x\ny", buf, sizeof(buf)) >= 0) {
    fail("format_accepted_newline_in_resource"); return;
  }
  if (cap_deny_marker_format(1u, (capability_id_t)9999, "-", buf, sizeof(buf)) >= 0) {
    fail("format_accepted_unknown_cap"); return;
  }
  /* Tiny buffer must fail rather than truncate silently. */
  char tiny[8];
  if (cap_deny_marker_format(1u, CAP_CONSOLE_WRITE, "-", tiny, sizeof(tiny)) >= 0) {
    fail("format_accepted_truncation"); return;
  }
  printf("TEST:PASS:cap_deny_marker_shape_negative\n");
}

/* ---- Phase 4: every capability_id_t value in capability.h has a registered
 * marker name. New caps added to the enum without updating the table will
 * trip this. */
static void test_cap_table_coverage(void) {
  static const capability_id_t all_caps[] = {
      CAP_CONSOLE_WRITE, CAP_SERIAL_WRITE, CAP_DEBUG_EXIT,
      CAP_CAPABILITY_ADMIN, CAP_DISK_IO_REQUEST, CAP_FS_READ, CAP_FS_WRITE,
      CAP_EVENT_SUBSCRIBE, CAP_EVENT_PUBLISH, CAP_APP_EXEC,
      CAP_CODESIGN_BYPASS, CAP_NETWORK,
      /* M1 IPC + reserved syscall caps. These were missing from the
       * cdm_cap_names[] table even though CAP_IPC_SEND/RECV are wired
       * into a live deny path (kernel/ipc/ipc_ops.c). Adding them here
       * pins the table against silent drift the next time the enum
       * grows. */
      CAP_IPC_SEND, CAP_IPC_RECV, CAP_SYSCALL,
  };
  const size_t n = sizeof(all_caps) / sizeof(all_caps[0]);
  for (size_t i = 0u; i < n; ++i) {
    if (cap_deny_marker_name(all_caps[i]) == NULL) {
      char r[64];
      snprintf(r, sizeof(r), "cap_table_missing:id=%d", (int)all_caps[i]);
      fail(r);
      return;
    }
  }
  /* Unknown id must NOT have a name. */
  if (cap_deny_marker_name((capability_id_t)0xDEADu) != NULL) {
    fail("cap_table_phantom_name"); return;
  }
  printf("TEST:PASS:cap_deny_marker_shape_cap_table\n");
}

int main(void) {
  test_format_roundtrip();
  if (g_fail) return 1;
  test_drivers();
  if (g_fail) return 1;
  test_negatives();
  if (g_fail) return 1;
  test_cap_table_coverage();
  if (g_fail) return 1;

  printf("TEST:PASS:cap_deny_marker_shape\n");
  return 0;
}
