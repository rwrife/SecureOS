/**
 * @file hello.c
 * @brief Canonical in-OS compiler validation sample for SecureOS.
 *
 * Purpose:
 *   This is the single reference "hello world" source compiled inside a
 *   running SecureOS instance to validate the in-OS toolchain path:
 *
 *       cc /apps/dev/hello.c -o /apps/hello.bin
 *       hello
 *
 *   Expected output:
 *
 *       hello from inside SecureOS
 *
 * Attribution / reuse:
 *   SecureOS project sample source intended for permissive reuse in local
 *   validation, tutorials, and compatibility tests.
 *
 * Notes:
 *   - `osapi.h` is the 8.3-safe staged alias of the public syscall surface.
 *     The in-OS compiler searches /apps/dev/include without an -I flag.
 *   - `os_console_write` needs no capability beyond the console the shell
 *     already holds, so this sample declares no special capabilities.
 *   - The on-disk filename is 8.3 (HELLO.C): the SecureOS filesystem caps
 *     names at 8 chars + 3-char extension, so "hello_world.c" is stored as
 *     "hello.c". See /apps/dev/building.txt for the full constraint list.
 */

#include "osapi.h"

int main(void) {
  os_console_write("hello from inside SecureOS\n");
  return 0;
}
