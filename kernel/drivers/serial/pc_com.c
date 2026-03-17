/**
 * @file pc_com.c
 * @brief Standard PC COM (16550-compatible UART) serial driver.
 *
 * Purpose:
 *   Implements a concrete serial backend for legacy PC COM ports. The
 *   driver configures UART registers at a selected I/O base and exposes
 *   non-blocking RX and blocking TX routines through serial HAL.
 *
 * Interactions:
 *   - hal/serial_hal.c: receives this driver's backend registration.
 *   - core/kmain.c: calls pc_com_serial_init_primary() to activate COM1.
 *
 * Launched by:
 *   Invoked during kernel startup. Not a standalone process; compiled into
 *   the kernel image.
 */

#include "pc_com.h"

#include "../../hal/serial_hal.h"

#define PC_COM_DEFAULT_BASE 0x3F8u

static unsigned short g_pc_com_base;

static inline void pc_com_outb(unsigned short port, unsigned char value) {
  __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char pc_com_inb(unsigned short port) {
  unsigned char value;
  __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static int pc_com_tx_empty(void) {
  return (pc_com_inb((unsigned short)(g_pc_com_base + 5u)) & 0x20u) != 0u;
}

static int pc_com_rx_ready(void) {
  return (pc_com_inb((unsigned short)(g_pc_com_base + 5u)) & 0x01u) != 0u;
}

static int pc_com_init(void) {
  pc_com_outb((unsigned short)(g_pc_com_base + 1u), 0x00u);
  pc_com_outb((unsigned short)(g_pc_com_base + 3u), 0x80u);
  pc_com_outb((unsigned short)(g_pc_com_base + 0u), 0x03u);
  pc_com_outb((unsigned short)(g_pc_com_base + 1u), 0x00u);
  pc_com_outb((unsigned short)(g_pc_com_base + 3u), 0x03u);
  pc_com_outb((unsigned short)(g_pc_com_base + 2u), 0xC7u);
  pc_com_outb((unsigned short)(g_pc_com_base + 4u), 0x0Bu);
  return 1;
}

static int pc_com_try_read_char(char *out_char) {
  if (out_char == 0 || !pc_com_rx_ready()) {
    return 0;
  }

  *out_char = (char)pc_com_inb(g_pc_com_base);
  return 1;
}

static void pc_com_write_char(char value) {
  while (!pc_com_tx_empty()) {
  }
  pc_com_outb(g_pc_com_base, (unsigned char)value);
}

static void pc_com_write(const char *message) {
  if (message == 0) {
    return;
  }

  while (*message != '\0') {
    if (*message == '\n') {
      pc_com_write_char('\r');
    }
    pc_com_write_char(*message++);
  }
}

static const serial_device_t g_pc_com_device = {
  SERIAL_BACKEND_PC_COM,
  "pc-com",
  pc_com_init,
  pc_com_try_read_char,
  pc_com_write_char,
  pc_com_write,
};

int pc_com_serial_init_primary(void) {
  return pc_com_serial_init_primary_at((unsigned short)PC_COM_DEFAULT_BASE);
}

int pc_com_serial_init_primary_at(unsigned short io_base) {
  if (io_base == 0u) {
    return 0;
  }

  g_pc_com_base = io_base;
  serial_hal_register_primary(&g_pc_com_device);
  return serial_hal_init();
}
