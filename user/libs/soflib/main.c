/**
 * @file main.c
 * @brief Marker source for the soflib loadable library.
 *
 * Purpose:
 *   Provides a minimal ELF artifact for the soflib user-space library so it
 *   can be loaded via the `loadlib` mechanism.  The actual library API is
 *   header-only in user/include/lib/soflib.h; this file exists solely to
 *   produce a .lib SOF container that the OS can register and track.
 *
 * Interactions:
 *   - Built by build/scripts/build_user_lib.sh into lib/soflib.lib.
 *   - The header user/include/lib/soflib.h contains the real API.
 *   - Loaded at runtime via the `loadlib soflib` command.
 *
 * Launched by:
 *   Build system compiles this into a .lib SOF file placed at /lib/soflib.lib
 *   during filesystem initialization.
 */

#include "lib/soflib.h"

int main(void) {
  (void)SOFLIB_HANDLE_INVALID;
  return 0;
}