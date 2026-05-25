/**
 * @file sosh_builtins.c
 * @brief sosh built-in command recognition.
 *
 * Purpose:
 *   Provides a lookup to determine if a command name is handled as a
 *   built-in by the sosh interpreter (set, echo, export, source, exit,
 *   return, and control flow keywords).
 *
 * Interactions:
 *   - sosh_builtins.h: public API.
 *   - sosh_eval.c: calls sosh_is_builtin to decide dispatch path.
 *
 * Launched by:
 *   Called internally by the sosh evaluator. Not a standalone binary.
 */

#include "sosh_builtins.h"

static int streq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b) return 0;
    a++;
    b++;
  }
  return *a == *b;
}

int sosh_is_builtin(const char *cmd) {
  if (cmd == 0) return 0;
  if (streq(cmd, "set")) return 1;
  if (streq(cmd, "echo")) return 1;
  if (streq(cmd, "export")) return 1;
  if (streq(cmd, "source")) return 1;
  if (streq(cmd, "exit")) return 1;
  if (streq(cmd, "return")) return 1;
  if (streq(cmd, "if")) return 1;
  if (streq(cmd, "elif")) return 1;
  if (streq(cmd, "else")) return 1;
  if (streq(cmd, "end")) return 1;
  if (streq(cmd, "while")) return 1;
  if (streq(cmd, "for")) return 1;
  return 0;
}
