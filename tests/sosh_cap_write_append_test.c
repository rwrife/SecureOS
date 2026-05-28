/**
 * @file sosh_cap_write_append_test.c
 * @brief Allow + deny acceptance for sosh `write <path> <content>` and
 *        `append <path> <content>` external commands gated on
 *        SOSH_CAP_FS_WRITE.
 *
 * Fifth enforcement slice of #351 (parent epic). Earlier slices:
 *   - echo  → SOSH_CAP_CONSOLE_WRITE                         (PR #358)
 *   - source / external-cmd → SOSH_CAP_FS_READ / APP_EXEC    (PR #367)
 *   - exists → SOSH_CAP_FS_READ (in conditional)             (PR #379)
 *   - cat / ls → SOSH_CAP_FS_READ (external-cmd routing)     (PR #381)
 *
 * This slice pins the docs/abi/sosh-capability-contract.md §4 row:
 *   - `> <path>` / `write` builtin → os_fs_write_file → SOSH_CAP_FS_WRITE
 *
 * Both `write` and `append` reach the generic external-command dispatch
 * site in sosh_eval.c and bind to `user/apps/sosh/main.c`'s
 * `sosh_app_streq("write" / "append")` arms which call
 * `os_fs_write_file(path, content, 0|1)`. The §4 contract is keyed on
 * the underlying syscall, so the gate must route both to
 * SOSH_CAP_FS_WRITE with `resource = <path>` instead of the default
 * SOSH_CAP_APP_EXEC with `resource = <binary>`. Deny semantics follow
 * §6: no syscall runs, no output is emitted, embedder rc propagates
 * into $?, the script does NOT abort.
 *
 * Markers:
 *   TEST:PASS:sosh_cap_write_append:write_allow_cap_id_is_fs_write
 *   TEST:PASS:sosh_cap_write_append:write_allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_write_append:write_allow_invokes_exec
 *   TEST:PASS:sosh_cap_write_append:write_deny_blocks_exec
 *   TEST:PASS:sosh_cap_write_append:write_deny_no_output_leaked
 *   TEST:PASS:sosh_cap_write_append:write_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_write_append:write_deny_script_continues
 *   TEST:PASS:sosh_cap_write_append:append_allow_cap_id_is_fs_write
 *   TEST:PASS:sosh_cap_write_append:append_allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_write_append:append_allow_invokes_exec
 *   TEST:PASS:sosh_cap_write_append:append_deny_blocks_exec
 *   TEST:PASS:sosh_cap_write_append:append_deny_no_output_leaked
 *   TEST:PASS:sosh_cap_write_append:append_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_write_append:other_extcmd_still_uses_app_exec
 *   TEST:PASS:sosh_cap_write_append:legacy_null_cap_check_noop
 *   TEST:PASS:sosh_cap_write_append
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
  if (out_buf && out_buf_size > 0) {
    /* Write a sentinel so we can detect leakage on the deny path. */
    const char *sentinel = "WROTE";
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
  printf("TEST:FAIL:sosh_cap_write_append:%s\n", why);
  exit(1);
}

static void reset(test_ctx_t *ctx, int deny_for) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->deny_for_cap_id = deny_for;
}

int main(void) {
  printf("TEST:START:sosh_cap_write_append\n");

  sosh_state_t state;

  /* ============================================================
   * Scenario A: write ALLOW — cap id = SOSH_CAP_FS_WRITE,
   * resource = first arg (path), exec invoked.
   * ============================================================ */
  test_ctx_t ctx;
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *write_ok = "write /tmp/note.txt hello world\n";
  (void)sosh_eval_script(&state, write_ok, (int)strlen(write_ok), "test", "");

  if (ctx.cap_calls < 1) die("write_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_WRITE) die("write_allow_wrong_cap_id");
  printf("TEST:PASS:sosh_cap_write_append:write_allow_cap_id_is_fs_write\n");

  if (strcmp(ctx.last_cap_resource, "/tmp/note.txt") != 0) {
    die("write_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_write_append:write_allow_cap_resource_is_path\n");

  if (ctx.exec_calls != 1) die("write_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "write") != 0) {
    die("write_allow_wrong_exec_cmd");
  }
  printf("TEST:PASS:sosh_cap_write_append:write_allow_invokes_exec\n");

  /* ============================================================
   * Scenario B: write DENY — exec NOT invoked, no output leaked,
   * $? carries DENY_RC, script continues to next line.
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_WRITE);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *write_deny =
      "write /forbidden/file payload\n"
      "set okmarker = continued\n";
  (void)sosh_eval_script(&state, write_deny, (int)strlen(write_deny), "test", "");

  if (ctx.exec_calls != 0) die("write_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_write_append:write_deny_blocks_exec\n");

  if (ctx.out_len != 0) die("write_deny_output_leaked");
  printf("TEST:PASS:sosh_cap_write_append:write_deny_no_output_leaked\n");

  const char *q = sosh_vars_get(&state.vars, "?");
  if (q == NULL) die("write_deny_exit_unset");
  if (strcmp(q, "9") != 0) die("write_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_write_append:write_deny_exit_code_propagates\n");

  const char *ok = sosh_vars_get(&state.vars, "okmarker");
  if (ok == NULL || strcmp(ok, "continued") != 0) {
    die("write_deny_script_aborted");
  }
  printf("TEST:PASS:sosh_cap_write_append:write_deny_script_continues\n");

  /* ============================================================
   * Scenario C: append ALLOW — same routing as write.
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *append_ok = "append /var/log/sosh.log entry\n";
  (void)sosh_eval_script(&state, append_ok, (int)strlen(append_ok), "test", "");

  if (ctx.cap_calls < 1) die("append_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_WRITE) die("append_allow_wrong_cap_id");
  printf("TEST:PASS:sosh_cap_write_append:append_allow_cap_id_is_fs_write\n");

  if (strcmp(ctx.last_cap_resource, "/var/log/sosh.log") != 0) {
    die("append_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_write_append:append_allow_cap_resource_is_path\n");

  if (ctx.exec_calls != 1) die("append_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "append") != 0) {
    die("append_allow_wrong_exec_cmd");
  }
  printf("TEST:PASS:sosh_cap_write_append:append_allow_invokes_exec\n");

  /* ============================================================
   * Scenario D: append DENY — same shape as write deny.
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_WRITE);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *append_deny = "append /forbidden/log oops\n";
  (void)sosh_eval_script(&state, append_deny, (int)strlen(append_deny), "test", "");

  if (ctx.exec_calls != 0) die("append_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_write_append:append_deny_blocks_exec\n");

  if (ctx.out_len != 0) die("append_deny_output_leaked");
  printf("TEST:PASS:sosh_cap_write_append:append_deny_no_output_leaked\n");

  q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "9") != 0) die("append_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_write_append:append_deny_exit_code_propagates\n");

  /* ============================================================
   * Scenario E: an unrelated external command still routes through
   * SOSH_CAP_APP_EXEC with resource = <binary> — regression guard
   * for the routing branch we just added.
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *other = "apps/hello.bin alpha beta\n";
  (void)sosh_eval_script(&state, other, (int)strlen(other), "test", "");

  if (ctx.cap_calls < 1) die("other_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_APP_EXEC) die("other_wrong_cap_id");
  if (strcmp(ctx.last_cap_resource, "apps/hello.bin") != 0) {
    die("other_wrong_cap_resource");
  }
  if (ctx.exec_calls != 1) die("other_exec_not_invoked");
  printf("TEST:PASS:sosh_cap_write_append:other_extcmd_still_uses_app_exec\n");

  /* ============================================================
   * Scenario F: legacy cap_check == NULL — write must still dispatch
   * (preserves contract §5.1 host-process mode).
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  /* Intentionally do NOT call sosh_eval_set_cap_check — leaves NULL. */

  const char *legacy = "write /tmp/x.txt body\n";
  (void)sosh_eval_script(&state, legacy, (int)strlen(legacy), "test", "");

  if (ctx.cap_calls != 0) die("legacy_cap_check_invoked");
  if (ctx.exec_calls != 1) die("legacy_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "write") != 0) {
    die("legacy_wrong_exec_cmd");
  }
  printf("TEST:PASS:sosh_cap_write_append:legacy_null_cap_check_noop\n");

  /* Audit-integration SKIP per contract §6.1 (issue #389). */
  printf("TEST:SKIP:sosh_cap_write_append:audit_deny_recorded:"
         "sosh_audit_unwired_pending_issue_389\n");

  printf("TEST:PASS:sosh_cap_write_append\n");
  return 0;
}
