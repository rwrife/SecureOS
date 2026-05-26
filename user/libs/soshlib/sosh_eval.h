/**
 * @file sosh_eval.h
 * @brief sosh evaluator — interprets script text line by line.
 *
 * Purpose:
 *   The main interpreter loop for sosh scripts. Tokenizes each line,
 *   evaluates expressions (with variable expansion, substring, concat,
 *   arithmetic), handles control flow (if/elif/else/end, while/end,
 *   for/end), and dispatches external commands via a callback.
 *
 * Interactions:
 *   - sosh_lexer.c: tokenizes each line.
 *   - sosh_vars.c: variable storage and lookup.
 *   - sosh_builtins.c: identifies built-in commands.
 *   - sosh.h: the public API wraps this.
 *
 * Launched by:
 *   Called by sosh/main.c (the sosh binary) or by any embedder via sosh.h.
 *   Not a standalone binary.
 */

#ifndef SOSH_EVAL_H
#define SOSH_EVAL_H

#include "sosh_builtins.h"
#include "sosh_vars.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOSH_LINE_MAX       512
#define SOSH_NESTING_MAX    16
#define SOSH_SOURCE_MAX     4

typedef struct {
  sosh_var_table_t vars;
  sosh_output_fn   output;
  sosh_exec_fn     exec;
  void            *user_ctx;
  int              exit_requested;
  int              exit_code;
} sosh_state_t;

/**
 * Initialize interpreter state.
 */
void sosh_eval_init(sosh_state_t *state, sosh_output_fn output,
                    sosh_exec_fn exec, void *user_ctx);

/**
 * Execute a script from a text buffer.
 * script_name is used for $0.
 * args is the space-separated argument string (for $1..$9, $@).
 * Returns the script's exit code.
 */
int sosh_eval_script(sosh_state_t *state, const char *script,
                     int script_len, const char *script_name,
                     const char *args);

#ifdef __cplusplus
}
#endif

#endif /* SOSH_EVAL_H */
