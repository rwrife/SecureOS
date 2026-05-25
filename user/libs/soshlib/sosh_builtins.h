/**
 * @file sosh_builtins.h
 * @brief sosh built-in command handlers (echo, set, export, source, exit).
 *
 * Purpose:
 *   Declares the built-in commands recognized by the sosh interpreter that
 *   do not require spawning an external process. These are handled directly
 *   by the evaluator.
 *
 * Interactions:
 *   - sosh_eval.c: dispatches built-in commands through these handlers.
 *   - sosh_vars.c: built-ins modify the variable table.
 *
 * Launched by:
 *   Called internally by the sosh evaluator. Not a standalone binary.
 */

#ifndef SOSH_BUILTINS_H
#define SOSH_BUILTINS_H

#include "sosh_vars.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback type for output (print to console) */
typedef void (*sosh_output_fn)(const char *text, void *ctx);

/* Callback type for executing external commands and capturing output */
typedef int (*sosh_exec_fn)(const char *command, const char *args,
                            char *out_buf, int out_buf_size, void *ctx);

/**
 * Check if a command name is a built-in.
 * Returns 1 if built-in, 0 otherwise.
 */
int sosh_is_builtin(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* SOSH_BUILTINS_H */
