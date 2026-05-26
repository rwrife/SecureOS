/**
 * @file sosh_vars.c
 * @brief sosh variable storage — fixed-size hash table implementation.
 *
 * Purpose:
 *   Implements variable storage for the sosh scripting language using a
 *   simple linear-probe table with fixed-size entries. Supports positional
 *   args ($0-$9, $@), last exit code ($?), and named variables.
 *
 * Interactions:
 *   - sosh_vars.h: public API.
 *   - sosh_eval.c / sosh_builtins.c: callers.
 *
 * Launched by:
 *   Called internally by the sosh interpreter. Not a standalone binary.
 */

#include "sosh_vars.h"

static int str_len(const char *s) {
  int len = 0;
  if (s == 0) return 0;
  while (s[len] != '\0') len++;
  return len;
}

static int str_eq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) return 0;
    a++;
    b++;
  }
  return *a == *b;
}

static void str_copy(char *dst, const char *src, int max) {
  int i = 0;
  if (src == 0) { dst[0] = '\0'; return; }
  while (src[i] != '\0' && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void int_to_str(int val, char *buf, int buf_size) {
  char tmp[16];
  int i = 0;
  int neg = 0;
  int pos;

  if (val < 0) {
    neg = 1;
    val = -val;
  }
  if (val == 0) {
    tmp[i++] = '0';
  } else {
    while (val > 0 && i < 15) {
      tmp[i++] = '0' + (val % 10);
      val /= 10;
    }
  }

  pos = 0;
  if (neg && pos < buf_size - 1) buf[pos++] = '-';
  while (i > 0 && pos < buf_size - 1) {
    buf[pos++] = tmp[--i];
  }
  buf[pos] = '\0';
}

void sosh_vars_init(sosh_var_table_t *tbl) {
  int i;
  if (tbl == 0) return;

  for (i = 0; i < SOSH_VAR_MAX; i++) {
    tbl->entries[i].used = 0;
    tbl->entries[i].name[0] = '\0';
    tbl->entries[i].value[0] = '\0';
  }
  for (i = 0; i < 10; i++) {
    tbl->args[i][0] = '\0';
  }
  tbl->arg_count = 0;
  tbl->all_args[0] = '\0';
  tbl->last_exit[0] = '0';
  tbl->last_exit[1] = '\0';
}

int sosh_vars_set(sosh_var_table_t *tbl, const char *name, const char *value) {
  int i;
  int first_empty = -1;

  if (tbl == 0 || name == 0) return -1;

  for (i = 0; i < SOSH_VAR_MAX; i++) {
    if (tbl->entries[i].used && str_eq(tbl->entries[i].name, name)) {
      str_copy(tbl->entries[i].value, value, SOSH_VAR_VALUE_MAX);
      return 0;
    }
    if (!tbl->entries[i].used && first_empty < 0) {
      first_empty = i;
    }
  }

  if (first_empty < 0) return -1; /* table full */

  tbl->entries[first_empty].used = 1;
  str_copy(tbl->entries[first_empty].name, name, SOSH_VAR_NAME_MAX);
  str_copy(tbl->entries[first_empty].value, value, SOSH_VAR_VALUE_MAX);
  return 0;
}

const char *sosh_vars_get(const sosh_var_table_t *tbl, const char *name) {
  int i;
  if (tbl == 0 || name == 0) return "";

  /* Check special vars */
  if (name[0] == '?' && name[1] == '\0') return tbl->last_exit;
  if (name[0] == '@' && name[1] == '\0') return tbl->all_args;

  /* Positional: $0-$9 */
  if (name[0] >= '0' && name[0] <= '9' && name[1] == '\0') {
    int idx = name[0] - '0';
    if (idx < tbl->arg_count) return tbl->args[idx];
    return "";
  }

  for (i = 0; i < SOSH_VAR_MAX; i++) {
    if (tbl->entries[i].used && str_eq(tbl->entries[i].name, name)) {
      return tbl->entries[i].value;
    }
  }
  return "";
}

void sosh_vars_set_args(sosh_var_table_t *tbl, const char *script_name,
                        const char *args_str) {
  int arg_idx = 1;
  int pos = 0;
  int all_pos = 0;

  if (tbl == 0) return;

  str_copy(tbl->args[0], script_name, SOSH_VAR_VALUE_MAX);

  if (args_str != 0) {
    while (args_str[pos] != '\0' && arg_idx < 10) {
      int start;
      /* skip spaces */
      while (args_str[pos] == ' ' || args_str[pos] == '\t') pos++;
      if (args_str[pos] == '\0') break;

      start = pos;
      while (args_str[pos] != '\0' && args_str[pos] != ' ' && args_str[pos] != '\t') {
        pos++;
      }

      /* Copy arg */
      {
        int len = pos - start;
        int j;
        if (len >= SOSH_VAR_VALUE_MAX) len = SOSH_VAR_VALUE_MAX - 1;
        for (j = 0; j < len; j++) {
          tbl->args[arg_idx][j] = args_str[start + j];
        }
        tbl->args[arg_idx][len] = '\0';
      }
      arg_idx++;
    }

    /* Build $@ */
    str_copy(tbl->all_args, args_str, SOSH_VAR_VALUE_MAX);
    (void)all_pos;
  }

  tbl->arg_count = arg_idx;
}

void sosh_vars_set_exit_code(sosh_var_table_t *tbl, int exit_code) {
  if (tbl == 0) return;
  int_to_str(exit_code, tbl->last_exit, sizeof(tbl->last_exit));
}

int sosh_vars_substring(const sosh_var_table_t *tbl, const char *name,
                        int start, int length, char *out_buf, int out_size) {
  const char *val;
  int val_len;
  int copy_len;
  int i;

  if (tbl == 0 || name == 0 || out_buf == 0 || out_size <= 0) return -1;

  val = sosh_vars_get(tbl, name);
  val_len = str_len(val);

  if (start < 0) start = 0;
  if (start >= val_len) {
    out_buf[0] = '\0';
    return 0;
  }

  if (length < 0) {
    /* No length specified: take rest of string */
    copy_len = val_len - start;
  } else {
    copy_len = length;
    if (start + copy_len > val_len) {
      copy_len = val_len - start;
    }
  }

  if (copy_len >= out_size) copy_len = out_size - 1;

  for (i = 0; i < copy_len; i++) {
    out_buf[i] = val[start + i];
  }
  out_buf[copy_len] = '\0';
  return 0;
}

int sosh_vars_length(const sosh_var_table_t *tbl, const char *name) {
  const char *val;
  if (tbl == 0 || name == 0) return -1;
  val = sosh_vars_get(tbl, name);
  return str_len(val);
}
