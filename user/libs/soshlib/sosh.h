/**
 * @file sosh.h
 * @brief sosh public API — embed the SecureOS Shell interpreter.
 *
 * Purpose:
 *   Provides the public interface for embedding the sosh scripting
 *   language interpreter in any application or kernel component.
 *   Consumers create a sosh_state_t, wire up output and exec callbacks,
 *   and call sosh_run_script() to interpret a script buffer.
 *
 * Interactions:
 *   - user/apps/sosh/main.c: the CLI binary uses this API.
 *   - kernel startup (phase 2): will use this to run boot scripts.
 *   - sosh_eval.c: implements the functions declared here.
 *
 * Launched by:
 *   Header-only; included by consumers of the sosh library.
 */

#ifndef SOSH_H
#define SOSH_H

#include "sosh_eval.h"
#include "sosh_lexer.h"
#include "sosh_vars.h"
#include "sosh_builtins.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convenience wrapper: initialize state and run a script in one call.
 * Returns the script's exit code.
 */
static inline int sosh_run_script(const char *script, int script_len,
                                  const char *script_name, const char *args,
                                  sosh_output_fn output, sosh_exec_fn exec,
                                  void *user_ctx) {
  sosh_state_t state;
  sosh_eval_init(&state, output, exec, user_ctx);
  return sosh_eval_script(&state, script, script_len, script_name, args);
}

#ifdef __cplusplus
}
#endif

#endif /* SOSH_H */
