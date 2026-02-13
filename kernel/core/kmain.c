#include "../arch/x86/serial.h"

__attribute__((used))
void kmain(void) {
  serial_init();
  serial_write("TEST:START:boot_entry\n");
  serial_write("KMAIN_REACHED\n");
  serial_write("TEST:PASS:boot_entry\n");

  for (;;) {
    __asm__ __volatile__("hlt");
  }
}
