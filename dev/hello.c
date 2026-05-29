/*
 * hello.c — SecureOS in-OS compiler validation sample.
 *
 * This is the "hello world" you compile from *inside* a running SecureOS
 * instance to confirm the in-OS toolchain works end to end:
 *
 *     cc /apps/dev/hello.c -o /apps/hello.bin
 *     hello
 *
 * Expected output on the console:
 *
 *     hello from inside SecureOS
 *
 * Notes
 * -----
 *   - `secureos_api.h` is the public syscall surface. The in-OS compiler
 *     ships it on its default include path (/apps/dev/include), so a bare
 *     #include resolves without any -I flag.
 *   - `os_console_write` needs no capability beyond the console the shell
 *     already holds, so this sample declares no special capabilities.
 *   - The on-disk filename is 8.3 (HELLO.C): the SecureOS filesystem caps
 *     names at 8 chars + 3-char extension, so "hello_world.c" is stored as
 *     "hello.c". See /apps/dev/building.txt for the full constraint list.
 */

#include "secureos_api.h"

int main(void) {
  os_console_write("hello from inside SecureOS\n");
  return 0;
}
