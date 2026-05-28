/**
 * @file sosh_cap_source_exec_test.c
 * @brief Allow + deny acceptance for sosh `source` (SOSH_CAP_FS_READ) and
 *        external command dispatch (SOSH_CAP_APP_EXEC).
 *
 * Second enforcement slice of #351 (echo/CONSOLE_WRITE landed in #358).
 * Pins docs/abi/sosh-capability-contract.md §4 rows:
 *   - `source <path>`                       → SOSH_CAP_FS_READ
 *   - `external command (apps/foo.bin ...)` → SOSH_CAP_APP_EXEC
 * and the §6 deny semantics (no side effect, $? propagation, no script
 * abort).
 *
 * Markers:
 *   TEST:PASS:sosh_cap_source_exec:source_allow_invokes_exec
 *   TEST:PASS:sosh_cap_source_exec:source_allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_source_exec:source_deny_blocks_exec
 *   TEST:PASS:sosh_cap_source_exec:source_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_source_exec:source_deny_script_continues
 *   TEST:PASS:sosh_cap_source_exec:extcmd_allow_invokes_exec
 *   TEST:PASS:sosh_cap_source_exec:extcmd_allow_cap_resource_is_binary
 *   TEST:PASS:sosh_cap_source_exec:extcmd_deny_blocks_exec
 *   TEST:PASS:sosh_cap_source_exec:extcmd_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_source_exec:extcmd_deny_no_output_leaked
 *   TEST:PASS:sosh_cap_source_exec
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/soshlib/sosh.h"

#define OUT_BUF_CAP 256
#define DENY_RC 9

typedef struct {
  char out[OUT_BUF_CAP];
  int  out_len;
  int  exec_calls;
  char last_exec_cmd[64];
  char last_exec_args[128];
  int  cap_calls;
  int  last_cap_id;
  char last_cap_resource[128];
  int  deny_for_cap_id;   /* if matches, return DENY_RC; else 0 */
} test_ctx_t;

static void capture_output(const char *text, void *vctx) {
  test_ctx_t *ctx = (test_ctx_t *)vctx;
  if (text == NULL) return;
  int len = (int)strlen(text);
  if (ctx->out_len + len >= OUT_BUF_CAP) len = OUT_BUF_CAP - 1 - ctx->out_len;
  if (len <= 0) return;
  memcpy(ctx->out + ctx->out_len, text, (size_t)len);
  ctx->out_len += len;
  ctx->out[ctx->out_len] = '\0';
}

/* Generic exec: records the call. For __cat_raw returns a tiny script
 * body so sosh_eval can re-enter; for other commands writes a sentinel
 * line into out_buf. */
static int recording_exec(const char *cmd, const char *args,
                          char *out_buf, int out_buf_size, void *vctx) {
  test_ctx_t *ctx = (test_ctx_t *)vctx;
  ctx->exec_calls++;
  if (cmd) {
    size_t n = strlen(cmd);
    if (n >= sizeof(ctx->last_exec_cmd)) n = sizeof(ctx->last_exec_cmd) - 1;
    memcpy(ctx->last_exec_cmd, cmd, n);
    ctx->last_exec_cmd[n] = '\0';
  }
  if (args) {
    size_t n = strlen(args);
    if (n >= sizeof(ctx->last_exec_args)) n = sizeof(ctx->last_exec_args) - 1;
    memcpy(ctx->last_exec_args, args, n);
    ctx->last_exec_args[n] = '\0';
  }
  if (cmd && strcmp(cmd, "__cat_raw") == 0) {
    /* Return a benign sourced-script body. Empty so the recursive
     * sosh_eval_script call is a no-op — we are only testing the gate. */
    if (out_buf && out_buf_size > 0) out_buf[0] = '\0';
    return 0;
  }
  if (out_buf && out_buf_size > 0) {
    const char *sentinel = "EXTERNAL_RAN";
    int n = (int)strlen(sentinel);
    if (n >= out_buf_size) n = out_buf_size - 1;
    memcpy(out_buf, sentinel, (size_t)n);
    out_buf[n] = '\0';
  }
  return 0;
}

static int recording_cap_check(int cap_id, const char *resource, void *vctx) {
  test_ctx_t *ctx = (test_ctx_t *)vctx;
  ctx->cap_calls++;
  ctx->last_cap_id = cap_id;
  ctx->last_cap_resource[0] = '\0';
  if (resource) {
    size_t n = strlen(resource);
    if (n >= sizeof(ctx->last_cap_resource)) {
      n = sizeof(ctx->last_cap_resource) - 1;
    }
    memcpy(ctx->last_cap_resource, resource, n);
    ctx->last_cap_resource[n] = '\0';
  }
  if (ctx->deny_for_cap_id != 0 && cap_id == ctx->deny_for_cap_id) {
    return DENY_RC;
  }
  return 0;
}

static void die(const char *why) {
  printf("TEST:FAIL:sosh_cap_source_exec:%s\n", why);
  exit(1);
}

static void reset(test_ctx_t *ctx, int deny_for) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->deny_for_cap_id = deny_for;
}

int main(void) {
  printf("TEST:START:sosh_cap_source_exec\n");

  /* ============================================================
   * Scenario A: source ALLOW
   * ============================================================ */
  test_ctx_t ctx;
  reset(&ctx, 0);
  sosh_state_t state;
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *src_ok = "source /etc/init.sosh\n";
  (void)sosh_eval_script(&state, src_ok, (int)strlen(src_ok), "test", "");

  if (ctx.cap_calls < 1) die("source_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_READ) die("source_allow_wrong_cap_id");
  if (strcmp(ctx.last_cap_resource, "/etc/init.sosh") != 0) {
    die("source_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_source_exec:source_allow_cap_resource_is_path\n");

  /* exec must have been invoked with __cat_raw */
  if (ctx.exec_calls < 1) die("source_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "__cat_raw") != 0) {
    die("source_allow_wrong_exec_cmd");
  }
  printf("TEST:PASS:sosh_cap_source_exec:source_allow_invokes_exec\n");

  /* ============================================================
   * Scenario B: source DENY + recovery line continues
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_READ);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  /* Allow APP_EXEC for the recovery `set` is implicit (set is not gated).
   * We use a 2-line script: a denied source followed by an `echo` that
   * is also gated. To keep this test self-contained against the echo
   * slice, instead use `set` (ungated) for the continuation probe. */
  const char *src_deny =
      "source /forbidden/file\n"
      "set okmarker = continued\n";
  (void)sosh_eval_script(&state, src_deny, (int)strlen(src_deny), "test", "");

  /* exec must NOT have been called (source short-circuited). */
  if (ctx.exec_calls != 0) die("source_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_source_exec:source_deny_blocks_exec\n");

  const char *q = sosh_vars_get(&state.vars, "?");
  if (q == NULL) die("source_deny_exit_unset");
  if (strcmp(q, "9") != 0) die("source_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_source_exec:source_deny_exit_code_propagates\n");

  const char *ok = sosh_vars_get(&state.vars, "okmarker");
  if (ok == NULL || strcmp(ok, "continued") != 0) {
    die("source_deny_script_aborted");
  }
  printf("TEST:PASS:sosh_cap_source_exec:source_deny_script_continues\n");

  /* ============================================================
   * Scenario C: external command ALLOW
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *ext_ok = "apps/hello.bin alpha beta\n";
  (void)sosh_eval_script(&state, ext_ok, (int)strlen(ext_ok), "test", "");

  if (ctx.cap_calls < 1) die("extcmd_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_APP_EXEC) die("extcmd_allow_wrong_cap_id");
  if (strcmp(ctx.last_cap_resource, "apps/hello.bin") != 0) {
    die("extcmd_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_source_exec:extcmd_allow_cap_resource_is_binary\n");

  if (ctx.exec_calls != 1) die("extcmd_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "apps/hello.bin") != 0) {
    die("extcmd_allow_wrong_exec_cmd");
  }
  if (strstr(ctx.out, "EXTERNAL_RAN") == NULL) {
    die("extcmd_allow_output_missing");
  }
  printf("TEST:PASS:sosh_cap_source_exec:extcmd_allow_invokes_exec\n");

  /* ============================================================
   * Scenario D: external command DENY
   * ============================================================ */
  reset(&ctx, SOSH_CAP_APP_EXEC);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *ext_deny =
      "apps/forbidden.bin\n"
      "set okmarker2 = continued\n";
  (void)sosh_eval_script(&state, ext_deny, (int)strlen(ext_deny),
                         "test", "");

  if (ctx.exec_calls != 0) die("extcmd_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_source_exec:extcmd_deny_blocks_exec\n");

  if (ctx.out_len != 0 || ctx.out[0] != '\0') die("extcmd_deny_output_leaked");
  if (strstr(ctx.out, "EXTERNAL_RAN") != NULL) die("extcmd_deny_sentinel_leaked");
  printf("TEST:PASS:sosh_cap_source_exec:extcmd_deny_no_output_leaked\n");

  q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "9") != 0) die("extcmd_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_source_exec:extcmd_deny_exit_code_propagates\n");

  const char *ok2 = sosh_vars_get(&state.vars, "okmarker2");
  if (ok2 == NULL || strcmp(ok2, "continued") != 0) {
    die("extcmd_deny_script_aborted");
  }

  /* Audit-integration SKIP per contract §6.1 (issue #389). */
  printf("TEST:SKIP:sosh_cap_source_exec:audit_deny_recorded:"
         "sosh_audit_unwired_pending_issue_389\n");

  printf("TEST:PASS:sosh_cap_source_exec\n");
  return 0;
}
