/**
 * @file sosh_cap_allow_test.c
 * @brief Allow-path acceptance test for sosh echo + SOSH_CAP_CONSOLE_WRITE.
 *
 * First enforcement slice of #351. Validates that when the embedder's
 * capability-check callback returns 0 (allow) for SOSH_CAP_CONSOLE_WRITE,
 * the `echo` builtin emits its text and sets $? = 0, per
 * docs/abi/sosh-capability-contract.md §4 + §6.
 *
 * Markers:
 *   TEST:PASS:sosh_cap_allow:echo_emits
 *   TEST:PASS:sosh_cap_allow:exit_code_zero
 *   TEST:PASS:sosh_cap_allow:cap_check_invoked
 *   TEST:PASS:sosh_cap_allow
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../user/libs/soshlib/sosh.h"

#define OUT_BUF_CAP 256

typedef struct {
  char out[OUT_BUF_CAP];
  int  out_len;
  int  cap_check_calls;
  int  last_cap_id;
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

static int cap_check_allow(int cap_id, const char *resource, void *vctx) {
  (void)resource;
  test_ctx_t *ctx = (test_ctx_t *)vctx;
  ctx->cap_check_calls++;
  ctx->last_cap_id = cap_id;
  return 0;
}

static void die(const char *why) {
  printf("TEST:FAIL:sosh_cap_allow:%s\n", why);
  exit(1);
}

int main(void) {
  printf("TEST:START:sosh_cap_allow\n");

  test_ctx_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  sosh_state_t state;
  sosh_eval_init(&state, capture_output, NULL, &ctx);
  sosh_eval_set_cap_check(&state, cap_check_allow, &ctx);

  const char *script = "echo hello-sosh\n";
  int rc = sosh_eval_script(&state, script, (int)strlen(script),
                            "test", "");
  if (rc != 0) die("nonzero_script_exit");

  /* echo must have emitted its text */
  if (strstr(ctx.out, "hello-sosh") == NULL) die("echo_did_not_emit");
  printf("TEST:PASS:sosh_cap_allow:echo_emits\n");

  /* gate must have been consulted exactly once, for the right cap */
  if (ctx.cap_check_calls != 1) die("cap_check_call_count");
  if (ctx.last_cap_id != SOSH_CAP_CONSOLE_WRITE) die("cap_check_wrong_cap");
  printf("TEST:PASS:sosh_cap_allow:cap_check_invoked\n");

  /* $? must be 0 after the allowed echo */
  const char *qmark = sosh_vars_get(&state.vars, "?");
  if (qmark == NULL || strcmp(qmark, "0") != 0) {
    die("exit_code_not_zero");
  }
  printf("TEST:PASS:sosh_cap_allow:exit_code_zero\n");

  printf("TEST:PASS:sosh_cap_allow\n");
  return 0;
}
