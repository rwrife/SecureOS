/**
 * @file sosh_cap_export_test.c
 * @brief Allow + deny acceptance for the sosh `export VAR=value`
 *        builtin's env-service write gated on SOSH_CAP_ENV_WRITE.
 *
 * Sixth enforcement slice of #351 (parent epic). Earlier slices:
 *   - echo                       -> SOSH_CAP_CONSOLE_WRITE   (PR #358)
 *   - source / external-cmd      -> SOSH_CAP_FS_READ / APP_EXEC (PR #367)
 *   - exists                     -> SOSH_CAP_FS_READ          (PR #379)
 *   - cat / ls                   -> SOSH_CAP_FS_READ          (PR #381)
 *   - write / append             -> SOSH_CAP_FS_WRITE         (PR #382)
 *
 * This slice closes the final §4 row of docs/abi/sosh-capability-contract.md:
 *   - `export VAR=value` -> env service write -> SOSH_CAP_ENV_WRITE
 *     with `resource = <var>`.
 *
 * Per the §6 footnote on `CAP_ENV_WRITE`, soshlib stays kernel-cap-
 * agnostic: it routes the gate through the abstract `SOSH_CAP_ENV_WRITE`
 * tag, letting the embedder either (a) map it to a future CAP_ENV_WRITE
 * or (b) return a deny rc so `export` becomes a host-only no-op for
 * sandboxed scripts. The in-process `sosh_vars_set` of the script
 * variable (the `set`-equivalent half of `export`) is NOT side-
 * effecting (it never leaves the interpreter) and runs regardless of
 * the cap_check verdict.
 *
 * Markers:
 *   TEST:PASS:sosh_cap_export:allow_cap_id_is_env_write
 *   TEST:PASS:sosh_cap_export:allow_cap_resource_is_var_name
 *   TEST:PASS:sosh_cap_export:allow_invokes_env_exec
 *   TEST:PASS:sosh_cap_export:allow_exec_payload_is_var_eq_value
 *   TEST:PASS:sosh_cap_export:allow_exit_code_zero
 *   TEST:PASS:sosh_cap_export:deny_blocks_env_exec
 *   TEST:PASS:sosh_cap_export:deny_var_still_set_in_script
 *   TEST:PASS:sosh_cap_export:deny_no_output_leaked
 *   TEST:PASS:sosh_cap_export:deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_export:deny_script_continues
 *   TEST:PASS:sosh_cap_export:set_unaffected_by_env_write_deny
 *   TEST:PASS:sosh_cap_export:legacy_null_cap_check_noop
 *   TEST:PASS:sosh_cap_export
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/soshlib/sosh.h"

#define OUT_BUF_CAP 256
#define DENY_RC 11

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
  (void)out_buf; (void)out_buf_size;
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
  printf("TEST:FAIL:sosh_cap_export:%s\n", why);
  exit(1);
}

static void reset(test_ctx_t *ctx, int deny_for) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->deny_for_cap_id = deny_for;
}

int main(void) {
  printf("TEST:START:sosh_cap_export\n");

  sosh_state_t state;

  /* ============================================================
   * Scenario A: ALLOW — cap id = SOSH_CAP_ENV_WRITE,
   *   resource = <var name>, env exec invoked with VAR=value payload,
   *   $? == 0.
   * ============================================================ */
  test_ctx_t ctx;
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *allow_src = "export FOO = bar\n";
  (void)sosh_eval_script(&state, allow_src, (int)strlen(allow_src), "test", "");

  if (ctx.cap_calls < 1) die("allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_ENV_WRITE) die("allow_wrong_cap_id");
  printf("TEST:PASS:sosh_cap_export:allow_cap_id_is_env_write\n");

  if (strcmp(ctx.last_cap_resource, "FOO") != 0) {
    die("allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_export:allow_cap_resource_is_var_name\n");

  if (ctx.exec_calls != 1) die("allow_env_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "env") != 0) die("allow_wrong_exec_cmd");
  printf("TEST:PASS:sosh_cap_export:allow_invokes_env_exec\n");

  if (strcmp(ctx.last_exec_args, "FOO=bar") != 0) {
    die("allow_wrong_exec_args");
  }
  printf("TEST:PASS:sosh_cap_export:allow_exec_payload_is_var_eq_value\n");

  const char *q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "0") != 0) die("allow_exit_code_not_zero");
  printf("TEST:PASS:sosh_cap_export:allow_exit_code_zero\n");

  /* ============================================================
   * Scenario B: DENY — env exec NOT invoked; the in-process script
   *   variable IS still updated (per `set`-equivalent semantics — not
   *   side-effecting, see contract §4 "Pure-language constructs" /
   *   §6 footnote on path-b); no output leak; $? carries DENY_RC;
   *   script continues to the next line.
   * ============================================================ */
  reset(&ctx, SOSH_CAP_ENV_WRITE);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *deny_src =
      "export SECRET = topsekret\n"
      "set okmarker = continued\n";
  (void)sosh_eval_script(&state, deny_src, (int)strlen(deny_src), "test", "");

  if (ctx.exec_calls != 0) die("deny_env_exec_invoked");
  printf("TEST:PASS:sosh_cap_export:deny_blocks_env_exec\n");

  const char *secret = sosh_vars_get(&state.vars, "SECRET");
  if (secret == NULL || strcmp(secret, "topsekret") != 0) {
    die("deny_in_process_set_lost");
  }
  printf("TEST:PASS:sosh_cap_export:deny_var_still_set_in_script\n");

  if (ctx.out_len != 0) die("deny_output_leaked");
  printf("TEST:PASS:sosh_cap_export:deny_no_output_leaked\n");

  q = sosh_vars_get(&state.vars, "?");
  if (q == NULL) die("deny_exit_unset");
  if (strcmp(q, "11") != 0) die("deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_export:deny_exit_code_propagates\n");

  const char *okm = sosh_vars_get(&state.vars, "okmarker");
  if (okm == NULL || strcmp(okm, "continued") != 0) {
    die("deny_script_aborted");
  }
  printf("TEST:PASS:sosh_cap_export:deny_script_continues\n");

  /* ============================================================
   * Scenario C: `set VAR=value` is NOT side-effecting and MUST NOT
   *   trigger SOSH_CAP_ENV_WRITE even with cap_check installed.
   *   Regression guard: we accidentally gated the wrong path otherwise.
   * ============================================================ */
  reset(&ctx, SOSH_CAP_ENV_WRITE);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *set_src = "set INTERNAL = ok\n";
  (void)sosh_eval_script(&state, set_src, (int)strlen(set_src), "test", "");

  const char *internal = sosh_vars_get(&state.vars, "INTERNAL");
  if (internal == NULL || strcmp(internal, "ok") != 0) {
    die("set_value_lost");
  }
  /* `set` MUST NOT call exec OR cap_check (no kernel surface). */
  if (ctx.exec_calls != 0) die("set_unexpected_exec_call");
  if (ctx.cap_calls != 0) die("set_unexpected_cap_check_call");
  printf("TEST:PASS:sosh_cap_export:set_unaffected_by_env_write_deny\n");

  /* ============================================================
   * Scenario D: legacy cap_check == NULL — `export` must still
   *   dispatch the env exec (preserves contract §5.1 host-process
   *   mode where the host is trusted and no gate is wired).
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  /* Intentionally do NOT call sosh_eval_set_cap_check — leaves NULL. */

  const char *legacy_src = "export X = y\n";
  (void)sosh_eval_script(&state, legacy_src, (int)strlen(legacy_src), "test", "");

  if (ctx.cap_calls != 0) die("legacy_cap_check_invoked");
  if (ctx.exec_calls != 1) die("legacy_env_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "env") != 0) die("legacy_wrong_exec_cmd");
  if (strcmp(ctx.last_exec_args, "X=y") != 0) die("legacy_wrong_exec_args");
  printf("TEST:PASS:sosh_cap_export:legacy_null_cap_check_noop\n");

  /* Audit-integration SKIP per contract §6.1 (issue #389). */
  printf("TEST:SKIP:sosh_cap_export:audit_deny_recorded:"
         "sosh_audit_unwired_pending_issue_389\n");

  printf("TEST:PASS:sosh_cap_export\n");
  return 0;
}
