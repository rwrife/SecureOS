/**
 * @file main.c
 * @brief fslib – shared library marker for filesystem helper APIs.
 *
 * Purpose:
 *   Marker ELF for the filesystem helper library. User apps import the
 *   API from lib/fslib.h, and this artifact is loadable via loadlib.
 */

#include "lib/fslib.h"

int main(void) {
  (void)FSLIB_HANDLE_INVALID;
  return 0;
}
