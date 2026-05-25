/**
 * @file sosh_vars.h
 * @brief sosh variable storage — fixed-size hash table for script variables.
 *
 * Purpose:
 *   Manages up to SOSH_VAR_MAX variables in a fixed-size table. Each
 *   variable has a name (up to 32 chars) and a string value (up to 256 chars).
 *   Supports get, set, expand (replace $VAR in strings), substring, and length.
 *
 * Interactions:
 *   - sosh_eval.c: uses this to store/retrieve variables during execution.
 *   - sosh_builtins.c: set/export commands modify variables here.
 *
 * Launched by:
 *   Called internally by the sosh interpreter. Not a standalone binary.
 */

#ifndef SOSH_VARS_H
#define SOSH_VARS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOSH_VAR_MAX       64
#define SOSH_VAR_NAME_MAX  32
#define SOSH_VAR_VALUE_MAX 256

typedef struct {
  char name[SOSH_VAR_NAME_MAX];
  char value[SOSH_VAR_VALUE_MAX];
  int  used;
} sosh_var_entry_t;

typedef struct {
  sosh_var_entry_t entries[SOSH_VAR_MAX];
  /* Positional args: $0..$9 */
  char args[10][SOSH_VAR_VALUE_MAX];
  int  arg_count;
  /* $@ (all args concatenated) */
  char all_args[SOSH_VAR_VALUE_MAX];
  /* $? (last exit code as string) */
  char last_exit[16];
} sosh_var_table_t;

/** Initialize the variable table to empty state. */
void sosh_vars_init(sosh_var_table_t *tbl);

/** Set a variable. Returns 0 on success, -1 if table full. */
int sosh_vars_set(sosh_var_table_t *tbl, const char *name, const char *value);

/** Get a variable value. Returns pointer to value or "" if not found. */
const char *sosh_vars_get(const sosh_var_table_t *tbl, const char *name);

/** Set positional args from a space-separated string. */
void sosh_vars_set_args(sosh_var_table_t *tbl, const char *script_name,
                        const char *args_str);

/** Set $? to exit_code. */
void sosh_vars_set_exit_code(sosh_var_table_t *tbl, int exit_code);

/**
 * Get substring of a variable: ${VAR:start:len}.
 * Writes result to out_buf (up to out_size-1 chars + null).
 * Returns 0 on success.
 */
int sosh_vars_substring(const sosh_var_table_t *tbl, const char *name,
                        int start, int length, char *out_buf, int out_size);

/** Get string length of a variable value. Returns -1 if var not found. */
int sosh_vars_length(const sosh_var_table_t *tbl, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SOSH_VARS_H */
