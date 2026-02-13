#include "serial.h"

#define COM1 0x3F8

static inline void outb(unsigned short port, unsigned char val) {
  __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
  unsigned char ret;
  __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

void serial_init(void) {
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x80);
  outb(COM1 + 0, 0x03);
  outb(COM1 + 1, 0x00);
  outb(COM1 + 3, 0x03);
  outb(COM1 + 2, 0xC7);
  outb(COM1 + 4, 0x0B);
}

static int serial_tx_empty(void) {
  return inb(COM1 + 5) & 0x20;
}

static void serial_write_char(char c) {
  while (!serial_tx_empty()) {
  }
  outb(COM1, (unsigned char)c);
}

void serial_write(const char *s) {
  for (; *s; s++) {
    if (*s == '\n') {
      serial_write_char('\r');
    }
    serial_write_char(*s);
  }
}
