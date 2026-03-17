/**
 * @file main.c
 * @brief "vgahello" display test app - exercises text rendering path.
 *
 * Purpose:
 *   Emits a deterministic text pattern to the console so the active video
 *   backend (VGA text mode or fallback backend) can be validated quickly in
 *   QEMU/manual runs.
 *
 * Interactions:
 *   - secureos_api.h: writes output through os_console_write syscall stub.
 *   - console/video HAL: kernel routes console text to serial and video.
 *
 * Launched by:
 *   Invoked as a user-space application via "run /apps/os/vgahello.bin".
 *   Built as a standalone ELF binary and wrapped as SOF binary.
 */

#include "secureos_api.h"

static void write_line(const char *value) {
  (void)os_console_write(value);
}

int main(void) {
  write_line("[vgahello] start\n");
  write_line("+------------------------------+\n");
  write_line("| SecureOS VGA/Video HAL Test  |\n");
  write_line("+------------------------------+\n");
  write_line("line 1: hello world\n");
  write_line("line 2: 0123456789 abcdefghijklmnopqrstuvwxyz\n");
  write_line("line 3: ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
  write_line("line 4: <>[]{}() !@#$%^&*_-+=\n");
  write_line("line 5: wrap-check wrap-check wrap-check wrap-check\n");
  write_line("[vgahello] done\n");
  return 0;
}
