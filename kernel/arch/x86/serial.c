/**
 * @file serial.c
 * @brief COM1 serial port driver for x86.
 *
 * Purpose:
 *   Initializes the COM1 serial port (I/O base 0x3F8) and provides
 *   character- and string-level output functions. Serial output is the
 *   primary debug/logging channel and is used alongside VGA for console
 *   output.
 *
 * Interactions:
 *   - console.c routes kernel console output through serial_putchar/
 *     serial_puts (in addition to VGA) so that log output is captured
 *     on the host terminal.
 *   - cap_gate.c wraps serial_puts behind the CAP_SERIAL_WRITE
 *     capability gate for controlled access.
 *   - Used indirectly by every subsystem that prints to the console.
 *
 * Launched by:
 *   serial_init() is called early in kmain() during kernel boot-up.
 *   Not a standalone process; compiled into the kernel image.
 */

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

static int serial_rx_ready(void) {
  return inb(COM1 + 5) & 0x01;
}

int serial_try_read_char(char *out_char) {
  if (out_char == 0) {
    return 0;
  }

  if (!serial_rx_ready()) {
    return 0;
  }

  *out_char = (char)inb(COM1);
  return 1;
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
