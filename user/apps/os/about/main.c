/**
 * @file main.c
 * @brief Source for the `about` OS command.
 *
 * Purpose:
 *   The `about` command takes a .bin or .lib filename as an argument
 *   and displays the file's SOF container metadata: name, description,
 *   author, version, date, file type, and signature status.
 *
 * Interactions:
 *   - Built by build/scripts/build_user_app.sh into os/about.bin.
 *   - Uses the soflib user-space library (user/include/lib/soflib.h)
 *     for SOF metadata type definitions.
 *   - At runtime, the kernel's script interpreter handles the `about`
 *     command via a built-in handler in kernel/user/process.c which
 *     reads the file from the filesystem and parses the SOF metadata.
 *
 * Launched by:
 *   Invoked from the console shell: `about <filename>`
 *   The script is `about $1\n` which dispatches to the kernel-side
 *   about handler.
 */

#include "lib/soflib.h"

int main(void) {
  (void)SOFLIB_HANDLE_INVALID;
  return 0;
}