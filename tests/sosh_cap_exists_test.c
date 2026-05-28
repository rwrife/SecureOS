/**
 * @file sosh_cap_exists_test.c
 * @brief Allow + deny acceptance for sosh `exists <path>` conditional
 *        builtin, gated on SOSH_CAP_FS_READ.
 *
 * Third enforcement slice of #351 (echo/CONSOLE_WRITE landed in #358;
 * source + external-cmd landed in #367). Pins
 * docs/abi/sosh-capability-contract.md §4 row:
 *   - `exists <path>` (in conditional) → SOSH_CAP_FS_READ
 * and §6 deny semantics: no underlying exec probe, $? carries the
 * embedder rc, condition evaluates false on deny, script does NOT abort.
 *
 * Markers:
 *   TEST:PASS:sosh_cap_exists:allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_exists:allow_invokes_exec
 *   TEST:PASS:sosh_cap_exists:allow_condition_true
 *   TEST:PASS:sosh_cap_exists:deny_blocks_exec
 *   TEST:PASS:sosh_cap_exists:deny_condition_false
 *   TEST:PASS:sosh_cap_exists:deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_exists:deny_script_continues
 *   TEST:PASS:sosh_cap_exists:legacy_null_cap_check_no_op
 *   TEST:PASS:sosh_cap_exists
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/soshlib/sosh.h"

#define OUT_BUF_CAP 256
#define DENY_RC 7

typedef struct {
  char out[OUT_BUF_CAP];
  int  out_len;
  int  exec_calls;
  char last_exec_cmd[64];
  char last_exec_args[128];
  int  cap_calls;
  int  last_cap_id;
  char last_cap_resource[128];
  int  deny_for_cap_id;
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

/* `exists` probe returns rc==0 ("exists") for every recorded path. */
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
  if (out_buf && out_buf_size > 0) out_buf[0] = '\0';
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
  printf("TEST:FAIL:sosh_cap_exists:%s\n", why);
  exit(1);
}

static void reset(test_ctx_t *ctx, int deny_for) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->deny_for_cap_id = deny_for;
}

int main(void) {
  printf("TEST:START:sosh_cap_exists\n");

  /* ============================================================
   * Scenario A: exists ALLOW
   * The `if exists <path>` branch should consult cap_check with
   * SOSH_CAP_FS_READ and the literal path, then invoke the exec
   * probe, then run the THEN body.
   * ============================================================ */
  test_ctx_t ctx;
  reset(&ctx, 0);
  sosh_state_t state;
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *allow_script =
      "if exists /etc/init.sosh\n"
      "  set marker = present\n"
      "end\n";
  (void)sosh_eval_script(&state, allow_script, (int)strlen(allow_script),
                         "test", "");

  if (ctx.cap_calls < 1) die("allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_READ) die("allow_wrong_cap_id");
  if (strcmp(ctx.last_cap_resource, "/etc/init.sosh") != 0) {
    die("allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_exists:allow_cap_resource_is_path\n");

  if (ctx.exec_calls < 1) die("allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "exists") != 0) die("allow_wrong_exec_cmd");
  if (strcmp(ctx.last_exec_args, "/etc/init.sosh") != 0) {
    die("allow_wrong_exec_args");
  }
  printf("TEST:PASS:sosh_cap_exists:allow_invokes_exec\n");

  const char *marker = sosh_vars_get(&state.vars, "marker");
  if (marker == NULL || strcmp(marker, "present") != 0) {
    die("allow_then_branch_skipped");
  }
  printf("TEST:PASS:sosh_cap_exists:allow_condition_true\n");

  /* ============================================================
   * Scenario B: exists DENY
   * The cap_check returns DENY_RC for SOSH_CAP_FS_READ. The exec
   * probe must NOT be invoked, the conditional must evaluate false
   * (else branch runs), $? must carry DENY_RC, and the script must
   * continue past the if/end block to the trailing `set`.
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_READ);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *deny_script =
      "if exists /forbidden/path\n"
      "  set then_marker = then_ran\n"
      "else\n"
      "  set else_marker = else_ran\n"
      "end\n"
      "set after_marker = continued\n";
  (void)sosh_eval_script(&state, deny_script, (int)strlen(deny_script),
                         "test", "");

  if (ctx.exec_calls != 0) die("deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_exists:deny_blocks_exec\n");

  const char *then_m = sosh_vars_get(&state.vars, "then_marker");
  const char *else_m = sosh_vars_get(&state.vars, "else_marker");
  if (then_m != NULL && strcmp(then_m, "") != 0) die("deny_then_ran");
  if (else_m == NULL || strcmp(else_m, "else_ran") != 0) {
    die("deny_else_not_taken");
  }
  printf("TEST:PASS:sosh_cap_exists:deny_condition_false\n");

  const char *q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "7") != 0) die("deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_exists:deny_exit_code_propagates\n");

  const char *after = sosh_vars_get(&state.vars, "after_marker");
  if (after == NULL || strcmp(after, "continued") != 0) {
    die("deny_script_aborted");
  }
  printf("TEST:PASS:sosh_cap_exists:deny_script_continues\n");

  /* ============================================================
   * Scenario C: legacy `cap_check == NULL` is a no-op (§5.1)
   * No cap_check is registered; the `exists` probe still runs and
   * the condition evaluates to its exec rc.
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  /* Deliberately do NOT call sosh_eval_set_cap_check. */

  const char *legacy_script =
      "if exists /etc/legacy.sosh\n"
      "  set legacy_marker = legacy_ran\n"
      "end\n";
  (void)sosh_eval_script(&state, legacy_script, (int)strlen(legacy_script),
                         "test", "");

  if (ctx.cap_calls != 0) die("legacy_cap_check_unexpectedly_called");
  if (ctx.exec_calls != 1) die("legacy_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "exists") != 0) {
    die("legacy_wrong_exec_cmd");
  }
  const char *legacy_marker = sosh_vars_get(&state.vars, "legacy_marker");
  if (legacy_marker == NULL || strcmp(legacy_marker, "legacy_ran") != 0) {
    die("legacy_then_branch_skipped");
  }
  printf("TEST:PASS:sosh_cap_exists:legacy_null_cap_check_no_op\n");

  printf("TEST:PASS:sosh_cap_exists\n");
  return 0;
}
