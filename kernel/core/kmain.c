#include "../arch/x86/serial.h"
#include "../arch/x86/vga.h"

__attribute__((used))
void kmain(void) {
  serial_init();
  vga_clear();

  serial_write("TEST:START:boot_entry\n");
  serial_write("Hello, SecureOS\n");
  vga_write("Hello, SecureOS\n");
  serial_write("KMAIN_REACHED\n");
  serial_write("TEST:PASS:boot_entry\n");

  for (;;) {
    __asm__ __volatile__("hlt");
  }
}
