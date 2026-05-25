/**
 * @file sosh_lexer.c
 * @brief sosh lexer — tokenizes a single script line into tokens.
 *
 * Purpose:
 *   Implements the tokenizer for the sosh scripting language. Handles
 *   quoted strings (with escape sequences), variable references ($VAR),
 *   substring expressions (${VAR:s:l}), length expressions (${#VAR}),
 *   command captures ($(cmd)), operators, and bare words.
 *
 * Interactions:
 *   - sosh_lexer.h: public API definitions.
 *   - sosh_parser.c / sosh_eval.c: consume the token list.
 *
 * Launched by:
 *   Called internally by sosh_eval during script interpretation.
 *   Not a standalone binary.
 */

#include "sosh_lexer.h"

static int is_space(char c) {
  return c == ' ' || c == '\t';
}

static int is_word_char(char c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  if (c == '_' || c == '/' || c == '.' || c == '-') return 1;
  return 0;
}

static int is_var_char(char c) {
  if (c >= 'a' && c <= 'z') return 1;
  if (c >= 'A' && c <= 'Z') return 1;
  if (c >= '0' && c <= '9') return 1;
  if (c == '_') return 1;
  return 0;
}

static void add_token(sosh_token_list_t *list, sosh_token_type_t type,
                      const char *value, int value_len) {
  sosh_token_t *tok;
  int copy_len;
  int i;

  if (list->count >= SOSH_TOKEN_MAX) return;

  tok = &list->tokens[list->count];
  tok->type = type;

  copy_len = value_len;
  if (copy_len >= SOSH_TOKEN_LEN_MAX) {
    copy_len = SOSH_TOKEN_LEN_MAX - 1;
  }
  for (i = 0; i < copy_len; i++) {
    tok->value[i] = value[i];
  }
  tok->value[copy_len] = '\0';
  list->count++;
}

static int lex_string(const char *line, int pos, sosh_token_list_t *out) {
  char buf[SOSH_TOKEN_LEN_MAX];
  int buf_len = 0;
  int i = pos + 1; /* skip opening quote */

  while (line[i] != '\0' && line[i] != '"') {
    if (line[i] == '\\' && line[i + 1] != '\0') {
      char escaped = line[i + 1];
      if (buf_len < SOSH_TOKEN_LEN_MAX - 1) {
        switch (escaped) {
          case 'n': buf[buf_len++] = '\n'; break;
          case 't': buf[buf_len++] = '\t'; break;
          case '\\': buf[buf_len++] = '\\'; break;
          case '"': buf[buf_len++] = '"'; break;
          default: buf[buf_len++] = escaped; break;
        }
      }
      i += 2;
    } else {
      if (buf_len < SOSH_TOKEN_LEN_MAX - 1) {
        buf[buf_len++] = line[i];
      }
      i++;
    }
  }

  if (line[i] == '"') i++; /* skip closing quote */

  add_token(out, SOSH_TOK_STRING, buf, buf_len);
  return i;
}

static int lex_dollar(const char *line, int pos, sosh_token_list_t *out) {
  int i = pos + 1; /* skip '$' */
  char buf[SOSH_TOKEN_LEN_MAX];
  int buf_len = 0;

  if (line[i] == '(') {
    /* Command capture: $(command args...) */
    int depth = 1;
    i++; /* skip '(' */
    while (line[i] != '\0' && depth > 0) {
      if (line[i] == '(') depth++;
      else if (line[i] == ')') {
        depth--;
        if (depth == 0) { i++; break; }
      }
      if (depth > 0 && buf_len < SOSH_TOKEN_LEN_MAX - 1) {
        buf[buf_len++] = line[i];
      }
      i++;
    }
    add_token(out, SOSH_TOK_CAPTURE, buf, buf_len);
    return i;
  }

  if (line[i] == '{') {
    i++; /* skip '{' */

    if (line[i] == '#') {
      /* ${#VAR} — string length */
      i++; /* skip '#' */
      while (line[i] != '\0' && line[i] != '}') {
        if (buf_len < SOSH_TOKEN_LEN_MAX - 1) buf[buf_len++] = line[i];
        i++;
      }
      if (line[i] == '}') i++;
      add_token(out, SOSH_TOK_VARLEN, buf, buf_len);
      return i;
    }

    /* ${VAR:start:len} — substring */
    while (line[i] != '\0' && line[i] != '}') {
      if (buf_len < SOSH_TOKEN_LEN_MAX - 1) buf[buf_len++] = line[i];
      i++;
    }
    if (line[i] == '}') i++;
    add_token(out, SOSH_TOK_VARSUBSTR, buf, buf_len);
    return i;
  }

  /* Simple variable: $VAR, $1, $@, $? */
  if (line[i] == '@' || line[i] == '?') {
    buf[0] = line[i];
    add_token(out, SOSH_TOK_VAR, buf, 1);
    return i + 1;
  }

  while (is_var_char(line[i]) && buf_len < SOSH_TOKEN_LEN_MAX - 1) {
    buf[buf_len++] = line[i++];
  }

  if (buf_len > 0) {
    add_token(out, SOSH_TOK_VAR, buf, buf_len);
  }
  return i;
}

int sosh_lex_line(const char *line, sosh_token_list_t *out) {
  int i = 0;

  out->count = 0;

  if (line == 0) return -1;

  while (line[i] != '\0') {
    /* Skip whitespace */
    while (is_space(line[i])) i++;
    if (line[i] == '\0' || line[i] == '#') break;

    /* String literal */
    if (line[i] == '"') {
      i = lex_string(line, i, out);
      continue;
    }

    /* Variable / capture / substring */
    if (line[i] == '$') {
      i = lex_dollar(line, i, out);
      continue;
    }

    /* Operators */
    if (line[i] == '=' && line[i + 1] == '=') {
      add_token(out, SOSH_TOK_OP_EQ, "==", 2);
      i += 2;
      continue;
    }
    if (line[i] == '!' && line[i + 1] == '=') {
      add_token(out, SOSH_TOK_OP_NEQ, "!=", 2);
      i += 2;
      continue;
    }
    if (line[i] == '<' && line[i + 1] == '=') {
      add_token(out, SOSH_TOK_OP_LTE, "<=", 2);
      i += 2;
      continue;
    }
    if (line[i] == '>' && line[i + 1] == '=') {
      add_token(out, SOSH_TOK_OP_GTE, ">=", 2);
      i += 2;
      continue;
    }
    if (line[i] == '<') {
      add_token(out, SOSH_TOK_OP_LT, "<", 1);
      i++;
      continue;
    }
    if (line[i] == '>') {
      add_token(out, SOSH_TOK_OP_GT, ">", 1);
      i++;
      continue;
    }
    if (line[i] == '=' && line[i + 1] != '=') {
      add_token(out, SOSH_TOK_OP_ASSIGN, "=", 1);
      i++;
      continue;
    }
    if (line[i] == '+') {
      add_token(out, SOSH_TOK_OP_PLUS, "+", 1);
      i++;
      continue;
    }
    if (line[i] == '-' && !is_word_char(line[i + 1])) {
      add_token(out, SOSH_TOK_OP_MINUS, "-", 1);
      i++;
      continue;
    }

    /* Bare word */
    {
      char buf[SOSH_TOKEN_LEN_MAX];
      int buf_len = 0;
      while (line[i] != '\0' && !is_space(line[i]) && line[i] != '"' &&
             line[i] != '$' && line[i] != '+' && line[i] != '=' &&
             line[i] != '<' && line[i] != '>' && line[i] != '!' &&
             buf_len < SOSH_TOKEN_LEN_MAX - 1) {
        /* Allow - inside words (e.g., file-name, --flag) */
        buf[buf_len++] = line[i++];
      }
      if (buf_len > 0) {
        add_token(out, SOSH_TOK_WORD, buf, buf_len);
      }
    }
  }

  return 0;
}
