/**
 * @file main.c
 * @brief soshlib — shared library entry point (placeholder for dynamic loading).
 *
 * Purpose:
 *   Provides the entry point for soshlib when built as a standalone shared
 *   library binary. The actual interpreter logic lives in sosh_eval.c,
 *   sosh_lexer.c, sosh_vars.c, and sosh_builtins.c. This file exists to
 *   satisfy the build system's requirement for a main() in each library.
 *
 * Interactions:
 *   - process.c: this library ELF is loaded via the loadlib mechanism.
 *   - sosh.h: consumers include the public header to use the interpreter.
 *
 * Launched by:
 *   Loaded as a shared library via "loadlib soshlib" at the console.
 *   Built as a standalone ELF binary placed in /lib/.
 */

#include "sosh.h"

int main(void) {
  /* Library entry stub — real usage is via sosh_run_script() */
  return 0;
}
