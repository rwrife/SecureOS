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

/*
 * Capability check callback (first enforcement slice of #351).
 *
 * Invoked by the evaluator immediately before executing a side-effecting
 * builtin (see docs/abi/sosh-capability-contract.md §4). The embedder
 * maps the abstract cap id below to the underlying CAP_* and is responsible
 * for emitting the canonical CAP:DENY:<sid>:<cap_name>:<resource> marker
 * + audit-ring record on denial (per §6 of the contract). soshlib itself
 * has no kernel-cap dependency.
 *
 * Returns 0 on allow, non-zero on deny. `resource` may be NULL (e.g. for
 * `echo`, whose resource sentinel is the literal '-' per §4).
 *
 * If sosh_state_t.cap_check is NULL the evaluator skips the check (legacy
 * host-process mode — the host owns its own caps and is trusted not to
 * exceed them, see §5.1).
 */
typedef int (*sosh_cap_check_fn)(int sosh_cap_id, const char *resource,
                                 void *ctx);

/* Abstract capability ids consumed by sosh_cap_check_fn. Stable across
 * embedders; embedder maps to its native CAP_* on dispatch. Values are
 * arbitrary tags, not on-disk ABI. */
#define SOSH_CAP_CONSOLE_WRITE 1
#define SOSH_CAP_FS_READ       2
#define SOSH_CAP_FS_WRITE      3
#define SOSH_CAP_APP_EXEC      4

/**
 * Check if a command name is a built-in.
 * Returns 1 if built-in, 0 otherwise.
 */
int sosh_is_builtin(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* SOSH_BUILTINS_H */
