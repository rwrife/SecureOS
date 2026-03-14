#include "debug_exit.h"

#define DEBUG_EXIT_PORT 0xF4

static inline void outb(unsigned short port, unsigned char val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

void debug_exit_qemu(uint8_t code) {
  outb(DEBUG_EXIT_PORT, code);
}
