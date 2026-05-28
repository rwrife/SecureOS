/**
 * @file sosh_cap_cat_ls_test.c
 * @brief Allow + deny acceptance for sosh `cat <path>` and `ls <path>`
 *        external commands gated on SOSH_CAP_FS_READ.
 *
 * Fourth enforcement slice of #351 (parent epic). Earlier slices:
 *   - echo  → SOSH_CAP_CONSOLE_WRITE                (PR #358)
 *   - source / external-cmd → SOSH_CAP_FS_READ / APP_EXEC (PR #367)
 *   - exists → SOSH_CAP_FS_READ (in conditional)    (PR #379)
 *
 * This slice pins the docs/abi/sosh-capability-contract.md §4 rows:
 *   - `cat <path>` → os_fs_read_file  → SOSH_CAP_FS_READ
 *   - `ls <path>`  → os_fs_list_dir   → SOSH_CAP_FS_READ
 *
 * Both surfaces dispatch through the external-command path in
 * sosh_eval.c. The §4 contract is keyed on the *underlying syscall*,
 * not on dispatch shape, so the gate must route `cat` and `ls` to
 * SOSH_CAP_FS_READ (with resource = <path>) instead of the default
 * SOSH_CAP_APP_EXEC. Deny semantics follow §6: no syscall, no output,
 * embedder rc propagates into $?, script does NOT abort.
 *
 * Markers:
 *   TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_id_is_fs_read
 *   TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_cat_ls:cat_allow_invokes_exec
 *   TEST:PASS:sosh_cap_cat_ls:cat_deny_blocks_exec
 *   TEST:PASS:sosh_cap_cat_ls:cat_deny_no_output_leaked
 *   TEST:PASS:sosh_cap_cat_ls:cat_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_cat_ls:cat_deny_script_continues
 *   TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_id_is_fs_read
 *   TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_resource_is_path
 *   TEST:PASS:sosh_cap_cat_ls:ls_allow_invokes_exec
 *   TEST:PASS:sosh_cap_cat_ls:ls_deny_blocks_exec
 *   TEST:PASS:sosh_cap_cat_ls:ls_deny_no_output_leaked
 *   TEST:PASS:sosh_cap_cat_ls:ls_deny_exit_code_propagates
 *   TEST:PASS:sosh_cap_cat_ls:other_extcmd_still_uses_app_exec
 *   TEST:PASS:sosh_cap_cat_ls:legacy_null_cap_check_noop
 *   TEST:PASS:sosh_cap_cat_ls
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

/* Generic exec: records the call and emits a sentinel line so the test
 * can detect output-leak regressions on the deny paths. */
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
    const char *sentinel = "FS_READ_RAN";
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
  printf("TEST:FAIL:sosh_cap_cat_ls:%s\n", why);
  exit(1);
}

static void reset(test_ctx_t *ctx, int deny_for) {
  memset(ctx, 0, sizeof(*ctx));
  ctx->deny_for_cap_id = deny_for;
}

int main(void) {
  printf("TEST:START:sosh_cap_cat_ls\n");

  /* ============================================================
   * Scenario A: `cat <path>` ALLOW
   *   - cap_check invoked with SOSH_CAP_FS_READ and resource=<path>
   *   - exec runs with cmd="cat" args="<path>"
   * ============================================================ */
  test_ctx_t ctx;
  reset(&ctx, 0);
  sosh_state_t state;
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *cat_ok = "cat /etc/motd\n";
  (void)sosh_eval_script(&state, cat_ok, (int)strlen(cat_ok), "test", "");

  if (ctx.cap_calls < 1) die("cat_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_READ) die("cat_allow_wrong_cap_id");
  printf("TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_id_is_fs_read\n");

  if (strcmp(ctx.last_cap_resource, "/etc/motd") != 0) {
    die("cat_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_cat_ls:cat_allow_cap_resource_is_path\n");

  if (ctx.exec_calls != 1) die("cat_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "cat") != 0) die("cat_allow_wrong_exec_cmd");
  if (strcmp(ctx.last_exec_args, "/etc/motd") != 0) {
    die("cat_allow_wrong_exec_args");
  }
  printf("TEST:PASS:sosh_cap_cat_ls:cat_allow_invokes_exec\n");

  /* ============================================================
   * Scenario B: `cat <path>` DENY + script continues
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_READ);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *cat_deny =
      "cat /forbidden/secret\n"
      "set okmarker = continued\n";
  (void)sosh_eval_script(&state, cat_deny, (int)strlen(cat_deny),
                         "test", "");

  if (ctx.exec_calls != 0) die("cat_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_cat_ls:cat_deny_blocks_exec\n");

  if (ctx.out_len != 0 || ctx.out[0] != '\0') die("cat_deny_output_leaked");
  if (strstr(ctx.out, "FS_READ_RAN") != NULL) die("cat_deny_sentinel_leaked");
  printf("TEST:PASS:sosh_cap_cat_ls:cat_deny_no_output_leaked\n");

  const char *q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "9") != 0) die("cat_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_cat_ls:cat_deny_exit_code_propagates\n");

  const char *ok = sosh_vars_get(&state.vars, "okmarker");
  if (ok == NULL || strcmp(ok, "continued") != 0) {
    die("cat_deny_script_aborted");
  }
  printf("TEST:PASS:sosh_cap_cat_ls:cat_deny_script_continues\n");

  /* ============================================================
   * Scenario C: `ls <path>` ALLOW
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *ls_ok = "ls /apps\n";
  (void)sosh_eval_script(&state, ls_ok, (int)strlen(ls_ok), "test", "");

  if (ctx.cap_calls < 1) die("ls_allow_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_FS_READ) die("ls_allow_wrong_cap_id");
  printf("TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_id_is_fs_read\n");

  if (strcmp(ctx.last_cap_resource, "/apps") != 0) {
    die("ls_allow_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_cat_ls:ls_allow_cap_resource_is_path\n");

  if (ctx.exec_calls != 1) die("ls_allow_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "ls") != 0) die("ls_allow_wrong_exec_cmd");
  printf("TEST:PASS:sosh_cap_cat_ls:ls_allow_invokes_exec\n");

  /* ============================================================
   * Scenario D: `ls <path>` DENY
   * ============================================================ */
  reset(&ctx, SOSH_CAP_FS_READ);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *ls_deny = "ls /forbidden\n";
  (void)sosh_eval_script(&state, ls_deny, (int)strlen(ls_deny), "test", "");

  if (ctx.exec_calls != 0) die("ls_deny_exec_invoked");
  printf("TEST:PASS:sosh_cap_cat_ls:ls_deny_blocks_exec\n");

  if (ctx.out_len != 0 || ctx.out[0] != '\0') die("ls_deny_output_leaked");
  printf("TEST:PASS:sosh_cap_cat_ls:ls_deny_no_output_leaked\n");

  q = sosh_vars_get(&state.vars, "?");
  if (q == NULL || strcmp(q, "9") != 0) die("ls_deny_exit_not_propagated");
  printf("TEST:PASS:sosh_cap_cat_ls:ls_deny_exit_code_propagates\n");

  /* ============================================================
   * Scenario E: a non-cat/non-ls external command STILL routes to
   * SOSH_CAP_APP_EXEC (no regression to the #367 slice).
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  sosh_eval_set_cap_check(&state, recording_cap_check, &ctx);

  const char *ext_ok = "apps/hello.bin\n";
  (void)sosh_eval_script(&state, ext_ok, (int)strlen(ext_ok), "test", "");

  if (ctx.cap_calls < 1) die("other_extcmd_no_cap_call");
  if (ctx.last_cap_id != SOSH_CAP_APP_EXEC) {
    die("other_extcmd_misrouted_to_fs_read");
  }
  if (strcmp(ctx.last_cap_resource, "apps/hello.bin") != 0) {
    die("other_extcmd_cap_resource_mismatch");
  }
  printf("TEST:PASS:sosh_cap_cat_ls:other_extcmd_still_uses_app_exec\n");

  /* ============================================================
   * Scenario F: legacy NULL cap_check path is a no-op (preserves
   * the §5.1 host-process backcompat contract).
   * ============================================================ */
  reset(&ctx, 0);
  sosh_eval_init(&state, capture_output, recording_exec, &ctx);
  /* Note: cap_check stays NULL (default from sosh_eval_init). */

  const char *legacy = "cat /etc/motd\n";
  (void)sosh_eval_script(&state, legacy, (int)strlen(legacy), "test", "");

  if (ctx.cap_calls != 0) die("legacy_cap_check_invoked");
  if (ctx.exec_calls != 1) die("legacy_exec_not_invoked");
  if (strcmp(ctx.last_exec_cmd, "cat") != 0) die("legacy_wrong_exec_cmd");
  printf("TEST:PASS:sosh_cap_cat_ls:legacy_null_cap_check_noop\n");

  /* Audit-integration SKIP per contract §6.1 (issue #389). */
  printf("TEST:SKIP:sosh_cap_cat_ls:audit_deny_recorded:"
         "sosh_audit_unwired_pending_issue_389\n");

  printf("TEST:PASS:sosh_cap_cat_ls\n");
  return 0;
}
