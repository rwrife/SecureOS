/**
 * @file sosh_eval.c
 * @brief sosh evaluator — main interpreter loop with control flow.
 *
 * Purpose:
 *   Implements the core execution engine for sosh scripts. Processes
 *   lines sequentially, handling variable assignment (set), output (echo),
 *   conditionals (if/elif/else/end), loops (while/end, for/end),
 *   expression evaluation (concatenation, arithmetic, comparison),
 *   command output capture ($(...)), and external command dispatch.
 *
 * Interactions:
 *   - sosh_lexer.c: tokenizes each line before evaluation.
 *   - sosh_vars.c: all variable get/set operations.
 *   - sosh_builtins.c: identifies built-in vs external commands.
 *   - sosh_eval.h: public API header.
 *
 * Launched by:
 *   Called by sosh/main.c or any embedder. Not a standalone binary.
 */

#include "sosh_eval.h"
#include "sosh_lexer.h"

/* --- Utility functions -------------------------------------------------- */

static int sosh_strlen(const char *s) {
  int len = 0;
  if (s == 0) return 0;
  while (s[len]) len++;
  return len;
}

static void sosh_strcpy(char *dst, const char *src, int max) {
  int i = 0;
  if (src == 0) { dst[0] = '\0'; return; }
  while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
  dst[i] = '\0';
}

static void sosh_strcat(char *dst, const char *src, int max) {
  int dlen = sosh_strlen(dst);
  int i = 0;
  if (src == 0) return;
  while (src[i] && dlen + i < max - 1) {
    dst[dlen + i] = src[i];
    i++;
  }
  dst[dlen + i] = '\0';
}

static int sosh_streq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b) return 0;
    a++; b++;
  }
  return *a == *b;
}

static int sosh_atoi(const char *s) {
  int val = 0;
  int neg = 0;
  if (s == 0) return 0;
  if (*s == '-') { neg = 1; s++; }
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return neg ? -val : val;
}

static int sosh_is_numeric(const char *s) {
  if (s == 0 || *s == '\0') return 0;
  if (*s == '-') s++;
  if (*s == '\0') return 0;
  while (*s) {
    if (*s < '0' || *s > '9') return 0;
    s++;
  }
  return 1;
}

static void sosh_itoa(int val, char *buf, int buf_size) {
  char tmp[16];
  int i = 0, pos = 0, neg = 0;
  if (val < 0) { neg = 1; val = -val; }
  if (val == 0) { tmp[i++] = '0'; }
  else { while (val > 0 && i < 15) { tmp[i++] = '0' + (val % 10); val /= 10; } }
  if (neg && pos < buf_size - 1) buf[pos++] = '-';
  while (i > 0 && pos < buf_size - 1) buf[pos++] = tmp[--i];
  buf[pos] = '\0';
}

/* --- Expression evaluation --------------------------------------------- */

/**
 * Resolve a single token to its string value.
 * Handles VAR, VARSUBSTR, VARLEN, CAPTURE, STRING, WORD.
 */
static void resolve_token(const sosh_token_t *tok, sosh_state_t *state,
                          char *out, int out_size) {
  out[0] = '\0';

  switch (tok->type) {
    case SOSH_TOK_STRING:
    case SOSH_TOK_WORD:
      sosh_strcpy(out, tok->value, out_size);
      break;

    case SOSH_TOK_VAR:
      sosh_strcpy(out, sosh_vars_get(&state->vars, tok->value), out_size);
      break;

    case SOSH_TOK_VARLEN: {
      int len = sosh_vars_length(&state->vars, tok->value);
      if (len < 0) len = 0;
      sosh_itoa(len, out, out_size);
      break;
    }

    case SOSH_TOK_VARSUBSTR: {
      /* Format: "VAR:start:len" or "VAR:start" */
      char var_name[SOSH_VAR_NAME_MAX];
      int start = 0, length = -1;
      int i = 0, j = 0;
      int colon_count = 0;
      char start_str[16] = {0};
      char len_str[16] = {0};
      int si = 0, li = 0;

      /* Parse VAR name */
      while (tok->value[i] && tok->value[i] != ':' && j < SOSH_VAR_NAME_MAX - 1) {
        var_name[j++] = tok->value[i++];
      }
      var_name[j] = '\0';

      /* Parse start */
      if (tok->value[i] == ':') {
        i++;
        while (tok->value[i] && tok->value[i] != ':' && si < 15) {
          start_str[si++] = tok->value[i++];
        }
        start_str[si] = '\0';

        /* Check if start references a variable */
        if (start_str[0] == '$') {
          const char *sv = sosh_vars_get(&state->vars, start_str + 1);
          start = sosh_atoi(sv);
        } else {
          start = sosh_atoi(start_str);
        }
      }

      /* Parse length */
      if (tok->value[i] == ':') {
        i++;
        while (tok->value[i] && tok->value[i] != '}' && li < 15) {
          len_str[li++] = tok->value[i++];
        }
        len_str[li] = '\0';

        if (len_str[0] == '$') {
          const char *lv = sosh_vars_get(&state->vars, len_str + 1);
          length = sosh_atoi(lv);
        } else {
          length = sosh_atoi(len_str);
        }
      }

      sosh_vars_substring(&state->vars, var_name, start, length, out, out_size);
      (void)colon_count;
      break;
    }

    case SOSH_TOK_CAPTURE: {
      /* Execute command and capture output */
      if (state->exec) {
        /* Split capture value into command + args */
        char cmd[SOSH_VAR_VALUE_MAX];
        const char *args = "";
        int ci = 0;
        const char *val = tok->value;

        while (*val == ' ') val++;
        while (*val && *val != ' ' && ci < SOSH_VAR_VALUE_MAX - 1) {
          cmd[ci++] = *val++;
        }
        cmd[ci] = '\0';
        while (*val == ' ') val++;
        if (*val) args = val;

        state->exec(cmd, args, out, out_size, state->user_ctx);
        /* Trim trailing newline from captured output */
        {
          int olen = sosh_strlen(out);
          while (olen > 0 && (out[olen-1] == '\n' || out[olen-1] == '\r')) {
            out[--olen] = '\0';
          }
        }
      }
      break;
    }

    default:
      sosh_strcpy(out, tok->value, out_size);
      break;
  }
}

/**
 * Evaluate an expression from token list starting at *pos.
 * Handles concatenation (+) and simple arithmetic.
 * Updates *pos past consumed tokens.
 */
static void eval_expr(const sosh_token_list_t *tokens, int *pos,
                      sosh_state_t *state, char *out, int out_size) {
  char left[SOSH_VAR_VALUE_MAX];

  out[0] = '\0';
  if (*pos >= tokens->count) return;

  resolve_token(&tokens->tokens[*pos], state, left, sizeof(left));
  (*pos)++;

  sosh_strcpy(out, left, out_size);

  /* Handle chained + and - operators */
  while (*pos < tokens->count) {
    if (tokens->tokens[*pos].type == SOSH_TOK_OP_PLUS) {
      char right[SOSH_VAR_VALUE_MAX];
      (*pos)++;
      if (*pos >= tokens->count) break;
      resolve_token(&tokens->tokens[*pos], state, right, sizeof(right));
      (*pos)++;

      /* If both are numeric, do arithmetic; otherwise concatenate */
      if (sosh_is_numeric(out) && sosh_is_numeric(right)) {
        int result = sosh_atoi(out) + sosh_atoi(right);
        sosh_itoa(result, out, out_size);
      } else {
        sosh_strcat(out, right, out_size);
      }
    } else if (tokens->tokens[*pos].type == SOSH_TOK_OP_MINUS) {
      char right[SOSH_VAR_VALUE_MAX];
      (*pos)++;
      if (*pos >= tokens->count) break;
      resolve_token(&tokens->tokens[*pos], state, right, sizeof(right));
      (*pos)++;

      if (sosh_is_numeric(out) && sosh_is_numeric(right)) {
        int result = sosh_atoi(out) - sosh_atoi(right);
        sosh_itoa(result, out, out_size);
      } else {
        /* subtraction on non-numeric is a no-op */
        break;
      }
    } else {
      break;
    }
  }
}

/**
 * Evaluate a condition expression. Returns 1 for true, 0 for false.
 * Condition starts at tokens[*pos].
 */
static int eval_condition(const sosh_token_list_t *tokens, int *pos,
                          sosh_state_t *state) {
  char left[SOSH_VAR_VALUE_MAX];
  char right[SOSH_VAR_VALUE_MAX];

  if (*pos >= tokens->count) return 0;

  /* Check for "not" prefix */
  if (tokens->tokens[*pos].type == SOSH_TOK_WORD &&
      sosh_streq(tokens->tokens[*pos].value, "not")) {
    (*pos)++;
    return !eval_condition(tokens, pos, state);
  }

  /* Check for "exists" */
  if (tokens->tokens[*pos].type == SOSH_TOK_WORD &&
      sosh_streq(tokens->tokens[*pos].value, "exists")) {
    (*pos)++;
    if (*pos < tokens->count) {
      resolve_token(&tokens->tokens[*pos], state, left, sizeof(left));
      (*pos)++;
      /* Use exec callback to check existence via ls */
      if (state->exec) {
        char result[64];
        int rc = state->exec("exists", left, result, sizeof(result), state->user_ctx);
        return rc == 0;
      }
    }
    return 0;
  }

  /* Evaluate left side expression */
  eval_expr(tokens, pos, state, left, sizeof(left));

  /* Check for comparison operator */
  if (*pos < tokens->count) {
    sosh_token_type_t op = tokens->tokens[*pos].type;
    if (op == SOSH_TOK_OP_EQ || op == SOSH_TOK_OP_NEQ ||
        op == SOSH_TOK_OP_LT || op == SOSH_TOK_OP_GT ||
        op == SOSH_TOK_OP_LTE || op == SOSH_TOK_OP_GTE) {
      (*pos)++;
      eval_expr(tokens, pos, state, right, sizeof(right));

      switch (op) {
        case SOSH_TOK_OP_EQ:  return sosh_streq(left, right);
        case SOSH_TOK_OP_NEQ: return !sosh_streq(left, right);
        case SOSH_TOK_OP_LT:  return sosh_atoi(left) < sosh_atoi(right);
        case SOSH_TOK_OP_GT:  return sosh_atoi(left) > sosh_atoi(right);
        case SOSH_TOK_OP_LTE: return sosh_atoi(left) <= sosh_atoi(right);
        case SOSH_TOK_OP_GTE: return sosh_atoi(left) >= sosh_atoi(right);
        default: break;
      }
    }
  }

  /* Truthy check: non-empty and not "0" */
  return (left[0] != '\0' && !sosh_streq(left, "0"));
}

/* --- Control flow state ------------------------------------------------ */

typedef enum {
  SOSH_BLOCK_IF,
  SOSH_BLOCK_WHILE,
  SOSH_BLOCK_FOR,
} sosh_block_type_t;

typedef struct {
  sosh_block_type_t type;
  int active;          /* currently executing this branch */
  int done;           /* a branch already executed (for if/elif/else) */
  int loop_start;     /* line index of while/for start */
  /* For-loop state */
  char for_var[SOSH_VAR_NAME_MAX];
  char for_items[SOSH_VAR_VALUE_MAX];
  int  for_pos;       /* current position in for_items */
} sosh_block_t;

/* --- Main evaluator ---------------------------------------------------- */

void sosh_eval_init(sosh_state_t *state, sosh_output_fn output,
                    sosh_exec_fn exec, void *user_ctx) {
  if (state == 0) return;
  sosh_vars_init(&state->vars);
  state->output = output;
  state->exec = exec;
  state->user_ctx = user_ctx;
  state->exit_requested = 0;
  state->exit_code = 0;
  state->cap_check = 0;
  state->cap_ctx = 0;
}

void sosh_eval_set_cap_check(sosh_state_t *state,
                             sosh_cap_check_fn cap_check,
                             void *cap_ctx) {
  if (state == 0) return;
  state->cap_check = cap_check;
  state->cap_ctx = cap_ctx;
}

/**
 * Get the next for-loop item from the items string.
 * Returns 1 if an item was found, 0 if exhausted.
 */
static int for_next_item(sosh_block_t *block, char *item, int item_size) {
  int pos = block->for_pos;
  int i = 0;
  const char *items = block->for_items;

  /* Skip leading whitespace including newlines */
  while (items[pos] == ' ' || items[pos] == '\t' ||
         items[pos] == '\n' || items[pos] == '\r') pos++;
  if (items[pos] == '\0') return 0;

  while (items[pos] && items[pos] != ' ' && items[pos] != '\t' &&
         items[pos] != '\n' && items[pos] != '\r') {
    if (i < item_size - 1) {
      item[i++] = items[pos];
    }
    pos++;
  }
  item[i] = '\0';
  block->for_pos = pos;
  return 1;
}

/**
 * Parse lines from the script buffer into a line array.
 * Returns the total number of lines.
 */
static int parse_lines(const char *script, int script_len,
                       const char **line_starts, int *line_lens, int max_lines) {
  int count = 0;
  int i = 0;

  while (i < script_len && count < max_lines) {
    int start = i;
    while (i < script_len && script[i] != '\n') i++;
    line_starts[count] = &script[start];
    line_lens[count] = i - start;
    /* Strip trailing \r */
    if (line_lens[count] > 0 && script[start + line_lens[count] - 1] == '\r') {
      line_lens[count]--;
    }
    count++;
    if (i < script_len) i++; /* skip \n */
  }
  return count;
}

int sosh_eval_script(sosh_state_t *state, const char *script,
                     int script_len, const char *script_name,
                     const char *args) {
  /* We use a simple line-based interpreter with a block stack */
  #define MAX_LINES 512
  static const char *line_starts[MAX_LINES];
  static int line_lens[MAX_LINES];
  int num_lines;
  int pc; /* program counter (current line) */

  static sosh_block_t blocks[SOSH_NESTING_MAX];
  int block_depth = 0;

  static char line_buf[SOSH_LINE_MAX];
  static sosh_token_list_t tokens;

  if (state == 0 || script == 0) return -1;

  sosh_vars_set_args(&state->vars, script_name ? script_name : "sosh", args);
  state->exit_requested = 0;
  state->exit_code = 0;

  num_lines = parse_lines(script, script_len, line_starts, line_lens, MAX_LINES);

  for (pc = 0; pc < num_lines && !state->exit_requested; pc++) {
    int len = line_lens[pc];
    int pos;
    const char *cmd_val;
    int executing = 1;

    /* Copy line into buffer */
    if (len >= SOSH_LINE_MAX) len = SOSH_LINE_MAX - 1;
    {
      int ci;
      for (ci = 0; ci < len; ci++) line_buf[ci] = line_starts[pc][ci];
      line_buf[len] = '\0';
    }

    /* Skip empty lines and comments */
    {
      const char *p = line_buf;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == '\0' || *p == '#') continue;
      /* Skip shebang */
      if (p[0] == '#' && p[1] == '!') continue;
    }

    /* Tokenize */
    if (sosh_lex_line(line_buf, &tokens) != 0 || tokens.count == 0) continue;

    /* Determine if we're executing (check block stack) */
    {
      int bi;
      for (bi = 0; bi < block_depth; bi++) {
        if (!blocks[bi].active) { executing = 0; break; }
      }
    }

    cmd_val = tokens.tokens[0].value;

    /* --- Control flow keywords (always processed) --- */

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "if")) {
      if (block_depth < SOSH_NESTING_MAX) {
        blocks[block_depth].type = SOSH_BLOCK_IF;
        if (executing) {
          pos = 1;
          int cond = eval_condition(&tokens, &pos, state);
          blocks[block_depth].active = cond;
          blocks[block_depth].done = cond;
        } else {
          blocks[block_depth].active = 0;
          blocks[block_depth].done = 1; /* don't eval inner branches */
        }
        block_depth++;
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "elif")) {
      if (block_depth > 0 && blocks[block_depth - 1].type == SOSH_BLOCK_IF) {
        sosh_block_t *blk = &blocks[block_depth - 1];
        /* Check parent blocks are active */
        int parent_active = 1;
        int bi;
        for (bi = 0; bi < block_depth - 1; bi++) {
          if (!blocks[bi].active) { parent_active = 0; break; }
        }
        if (parent_active && !blk->done) {
          pos = 1;
          int cond = eval_condition(&tokens, &pos, state);
          blk->active = cond;
          if (cond) blk->done = 1;
        } else {
          blk->active = 0;
        }
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "else")) {
      if (block_depth > 0 && blocks[block_depth - 1].type == SOSH_BLOCK_IF) {
        sosh_block_t *blk = &blocks[block_depth - 1];
        int parent_active = 1;
        int bi;
        for (bi = 0; bi < block_depth - 1; bi++) {
          if (!blocks[bi].active) { parent_active = 0; break; }
        }
        if (parent_active && !blk->done) {
          blk->active = 1;
          blk->done = 1;
        } else {
          blk->active = 0;
        }
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "while")) {
      if (executing) {
        if (block_depth < SOSH_NESTING_MAX) {
          pos = 1;
          int cond = eval_condition(&tokens, &pos, state);
          blocks[block_depth].type = SOSH_BLOCK_WHILE;
          blocks[block_depth].active = cond;
          blocks[block_depth].loop_start = pc;
          block_depth++;
        }
      } else {
        /* Push inactive block for nesting tracking */
        if (block_depth < SOSH_NESTING_MAX) {
          blocks[block_depth].type = SOSH_BLOCK_WHILE;
          blocks[block_depth].active = 0;
          blocks[block_depth].loop_start = pc;
          block_depth++;
        }
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "for")) {
      /* for VAR in expr */
      if (block_depth < SOSH_NESTING_MAX) {
        blocks[block_depth].type = SOSH_BLOCK_FOR;
        blocks[block_depth].loop_start = pc;

        if (executing && tokens.count >= 4) {
          char var_name[SOSH_VAR_NAME_MAX];
          char items[SOSH_VAR_VALUE_MAX];
          char item[SOSH_VAR_VALUE_MAX];

          /* token[1] = VAR name, token[2] should be "in", token[3..] = expr */
          sosh_strcpy(var_name, tokens.tokens[1].value, SOSH_VAR_NAME_MAX);
          sosh_strcpy(blocks[block_depth].for_var, var_name, SOSH_VAR_NAME_MAX);

          /* Evaluate all tokens after "in" into items, space-separated */
          pos = 3;
          items[0] = '\0';
          while (pos < tokens.count) {
            char tok_val[SOSH_VAR_VALUE_MAX];
            resolve_token(&tokens.tokens[pos], state, tok_val, sizeof(tok_val));
            if (items[0] != '\0') sosh_strcat(items, " ", sizeof(items));
            sosh_strcat(items, tok_val, sizeof(items));
            pos++;
          }
          sosh_strcpy(blocks[block_depth].for_items, items, SOSH_VAR_VALUE_MAX);
          blocks[block_depth].for_pos = 0;

          /* Get first item */
          if (for_next_item(&blocks[block_depth], item, sizeof(item))) {
            sosh_vars_set(&state->vars, var_name, item);
            blocks[block_depth].active = 1;
          } else {
            blocks[block_depth].active = 0;
          }
        } else {
          blocks[block_depth].active = 0;
          blocks[block_depth].for_var[0] = '\0';
          blocks[block_depth].for_items[0] = '\0';
          blocks[block_depth].for_pos = 0;
        }
        block_depth++;
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "end")) {
      if (block_depth > 0) {
        sosh_block_t *blk = &blocks[block_depth - 1];

        if (blk->type == SOSH_BLOCK_WHILE && blk->active) {
          /* Re-evaluate condition at loop_start line */
          char loop_line[SOSH_LINE_MAX];
          sosh_token_list_t loop_tokens;
          int loop_len = line_lens[blk->loop_start];
          int li;

          if (loop_len >= SOSH_LINE_MAX) loop_len = SOSH_LINE_MAX - 1;
          for (li = 0; li < loop_len; li++) {
            loop_line[li] = line_starts[blk->loop_start][li];
          }
          loop_line[loop_len] = '\0';

          if (sosh_lex_line(loop_line, &loop_tokens) == 0) {
            pos = 1; /* skip "while" keyword */
            if (eval_condition(&loop_tokens, &pos, state)) {
              pc = blk->loop_start; /* jump back (will increment at top of for) */
              continue;
            }
          }
          block_depth--;
        } else if (blk->type == SOSH_BLOCK_FOR && blk->active) {
          /* Try next item */
          char item[SOSH_VAR_VALUE_MAX];
          if (for_next_item(blk, item, sizeof(item))) {
            sosh_vars_set(&state->vars, blk->for_var, item);
            pc = blk->loop_start; /* jump back */
            continue;
          }
          block_depth--;
        } else {
          block_depth--;
        }
      }
      continue;
    }

    /* If not executing, skip non-control-flow lines */
    if (!executing) continue;

    /* --- Executable statements --- */

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "set")) {
      /* set VAR = expr */
      if (tokens.count >= 4 && tokens.tokens[2].type == SOSH_TOK_OP_ASSIGN) {
        char var_name[SOSH_VAR_NAME_MAX];
        char value[SOSH_VAR_VALUE_MAX];
        sosh_strcpy(var_name, tokens.tokens[1].value, SOSH_VAR_NAME_MAX);
        pos = 3;
        eval_expr(&tokens, &pos, state, value, sizeof(value));
        sosh_vars_set(&state->vars, var_name, value);
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "export")) {
      /* export VAR = expr — same as set, but also calls exec to export */
      if (tokens.count >= 4 && tokens.tokens[2].type == SOSH_TOK_OP_ASSIGN) {
        char var_name[SOSH_VAR_NAME_MAX];
        char value[SOSH_VAR_VALUE_MAX];
        sosh_strcpy(var_name, tokens.tokens[1].value, SOSH_VAR_NAME_MAX);
        pos = 3;
        eval_expr(&tokens, &pos, state, value, sizeof(value));
        sosh_vars_set(&state->vars, var_name, value);
        /* Call env set via exec callback */
        if (state->exec) {
          char env_arg[SOSH_VAR_VALUE_MAX + SOSH_VAR_NAME_MAX + 2];
          sosh_strcpy(env_arg, var_name, sizeof(env_arg));
          sosh_strcat(env_arg, "=", sizeof(env_arg));
          sosh_strcat(env_arg, value, sizeof(env_arg));
          state->exec("env", env_arg, (char*)0, 0, state->user_ctx);
        }
      }
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "echo")) {
      /* echo expr...  — gated by SOSH_CAP_CONSOLE_WRITE per
       * docs/abi/sosh-capability-contract.md §4. Deny short-circuits the
       * builtin so no output is leaked (§6 bullet 3) and the script's
       * $? observes the embedder-supplied non-zero exit code. */
      char output[SOSH_VAR_VALUE_MAX];
      if (state->cap_check != 0) {
        int rc = state->cap_check(SOSH_CAP_CONSOLE_WRITE, (const char *)0,
                                  state->cap_ctx);
        if (rc != 0) {
          sosh_vars_set_exit_code(&state->vars, rc);
          continue;
        }
      }
      pos = 1;
      eval_expr(&tokens, &pos, state, output, sizeof(output));
      sosh_strcat(output, "\n", sizeof(output));
      if (state->output) state->output(output, state->user_ctx);
      sosh_vars_set_exit_code(&state->vars, 0);
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD &&
        (sosh_streq(cmd_val, "return") || sosh_streq(cmd_val, "exit"))) {
      if (tokens.count >= 2) {
        char code_str[16];
        pos = 1;
        resolve_token(&tokens.tokens[pos], state, code_str, sizeof(code_str));
        state->exit_code = sosh_atoi(code_str);
      }
      state->exit_requested = 1;
      continue;
    }

    if (tokens.tokens[0].type == SOSH_TOK_WORD && sosh_streq(cmd_val, "source")) {
      /* source filename — read file and eval it.
       * Gated by SOSH_CAP_FS_READ per docs/abi/sosh-capability-contract.md
       * §4 (row `source <path>`). Deny short-circuits the read so no file
       * content is leaked into the interpreter, $? observes the embedder
       * non-zero rc, and the script does NOT abort (§6). */
      if (tokens.count >= 2 && state->exec) {
        char path[SOSH_VAR_VALUE_MAX];
        char file_content[4096]; /* limited source file size */
        int rc;
        pos = 1;
        resolve_token(&tokens.tokens[pos], state, path, sizeof(path));
        if (state->cap_check != 0) {
          int crc = state->cap_check(SOSH_CAP_FS_READ, path, state->cap_ctx);
          if (crc != 0) {
            sosh_vars_set_exit_code(&state->vars, crc);
            continue;
          }
        }
        rc = state->exec("__cat_raw", path, file_content, sizeof(file_content),
                         state->user_ctx);
        if (rc == 0 && file_content[0] != '\0') {
          sosh_eval_script(state, file_content, sosh_strlen(file_content),
                           path, "");
        }
      }
      continue;
    }

    /* --- External command execution --- */
    {
      char cmd[SOSH_VAR_VALUE_MAX];
      char cmd_args[SOSH_VAR_VALUE_MAX];
      char out_buf[SOSH_VAR_VALUE_MAX];
      int rc;

      out_buf[0] = '\0';
      resolve_token(&tokens.tokens[0], state, cmd, sizeof(cmd));

      /* Build args from remaining tokens */
      cmd_args[0] = '\0';
      pos = 1;
      while (pos < tokens.count) {
        char tok_val[SOSH_VAR_VALUE_MAX];
        resolve_token(&tokens.tokens[pos], state, tok_val, sizeof(tok_val));
        if (cmd_args[0] != '\0') sosh_strcat(cmd_args, " ", sizeof(cmd_args));
        sosh_strcat(cmd_args, tok_val, sizeof(cmd_args));
        pos++;
      }

      if (state->exec) {
        /* Dispatch — gated per docs/abi/sosh-capability-contract.md §4.
         *
         * The §4 contract distinguishes the underlying syscall, not the
         * fact-of-dispatch: `cat <path>` and `ls <path>` reach
         * `os_fs_read_file` / `os_fs_list_dir` and therefore require
         * SOSH_CAP_FS_READ with `resource = <path>`. Every other external
         * command goes through `process_create` via launcher and requires
         * SOSH_CAP_APP_EXEC with `resource = <binary>`. soshlib stays
         * kernel-cap-agnostic — the embedder maps each abstract
         * SOSH_CAP_* to its native CAP_* and emits the canonical
         * CAP:DENY:<sid>:<cap_name>:<resource> marker per §6.
         *
         * Deny short-circuits the exec callback so no syscall runs and
         * no output is emitted; the embedder-supplied non-zero rc
         * surfaces in $? and the script continues (§6). */
        if (state->cap_check != 0) {
          int cap_id;
          const char *cap_resource;
          if (sosh_streq(cmd, "cat") || sosh_streq(cmd, "ls")) {
            cap_id = SOSH_CAP_FS_READ;
            cap_resource = cmd_args;
          } else {
            cap_id = SOSH_CAP_APP_EXEC;
            cap_resource = cmd;
          }
          int crc = state->cap_check(cap_id, cap_resource, state->cap_ctx);
          if (crc != 0) {
            sosh_vars_set_exit_code(&state->vars, crc);
            continue;
          }
        }
        rc = state->exec(cmd, cmd_args, out_buf, sizeof(out_buf), state->user_ctx);
        sosh_vars_set_exit_code(&state->vars, rc);
        /* Print output if any */
        if (out_buf[0] != '\0' && state->output) {
          state->output(out_buf, state->user_ctx);
        }
      }
    }
  }

  return state->exit_code;
  #undef MAX_LINES
}
