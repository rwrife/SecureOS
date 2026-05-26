/**
 * @file sosh_lexer.h
 * @brief sosh lexer — tokenizes a single script line into a token array.
 *
 * Purpose:
 *   Breaks a raw text line into discrete tokens: keywords, identifiers,
 *   string literals, variable references, operators, and special forms
 *   like ${VAR:start:len} and $(cmd). Used by sosh_parser to build
 *   statement nodes.
 *
 * Interactions:
 *   - sosh_parser.c: consumes token arrays produced here.
 *   - sosh_eval.c: uses token type info for expression evaluation.
 *
 * Launched by:
 *   Called by the sosh evaluator/parser during script interpretation.
 *   Not a standalone binary.
 */

#ifndef SOSH_LEXER_H
#define SOSH_LEXER_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOSH_TOKEN_MAX      32   /* max tokens per line */
#define SOSH_TOKEN_LEN_MAX  256  /* max chars per token value */

typedef enum {
  SOSH_TOK_EOF = 0,
  SOSH_TOK_WORD,        /* bare word or identifier */
  SOSH_TOK_STRING,      /* "quoted string" */
  SOSH_TOK_VAR,         /* $VAR or $1 etc */
  SOSH_TOK_VARSUBSTR,   /* ${VAR:start:len} */
  SOSH_TOK_VARLEN,      /* ${#VAR} */
  SOSH_TOK_CAPTURE,     /* $(command args) */
  SOSH_TOK_OP_PLUS,     /* + */
  SOSH_TOK_OP_MINUS,    /* - */
  SOSH_TOK_OP_EQ,       /* == */
  SOSH_TOK_OP_NEQ,      /* != */
  SOSH_TOK_OP_LT,       /* < */
  SOSH_TOK_OP_GT,       /* > */
  SOSH_TOK_OP_LTE,      /* <= */
  SOSH_TOK_OP_GTE,      /* >= */
  SOSH_TOK_OP_ASSIGN,   /* = */
  SOSH_TOK_NEWLINE,     /* end of line */
} sosh_token_type_t;

typedef struct {
  sosh_token_type_t type;
  char value[SOSH_TOKEN_LEN_MAX];
  /* For VARSUBSTR: stores "VAR:start:len" in value */
} sosh_token_t;

typedef struct {
  sosh_token_t tokens[SOSH_TOKEN_MAX];
  int count;
} sosh_token_list_t;

/**
 * Tokenize a single line of sosh script.
 * Returns 0 on success, -1 on error (e.g., unterminated string).
 */
int sosh_lex_line(const char *line, sosh_token_list_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SOSH_LEXER_H */
