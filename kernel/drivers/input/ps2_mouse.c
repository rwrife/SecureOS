/**
 * @file ps2_mouse.c
 * @brief PS/2 mouse driver for x86 8042 controller auxiliary device.
 *
 * Purpose:
 *   Implements mouse input by communicating with the PS/2 auxiliary device
 *   through the 8042 keyboard controller. Enables the mouse in streaming
 *   mode and reads 3-byte movement/button packets. Provides a polling API
 *   for the mouse HAL to consume.
 *
 * Interactions:
 *   - hal/mouse_hal.c: calls ps2_mouse_poll() to get movement events.
 *   - core/kmain.c: calls ps2_mouse_init() at boot (failure is non-fatal).
 *
 * Launched by:
 *   Invoked during kernel startup. Not a standalone process; compiled into
 *   the kernel image. If initialization fails, mouse is simply unavailable.
 */

#include "ps2_mouse.h"

#define PS2_DATA_PORT    0x60u
#define PS2_STATUS_PORT  0x64u
#define PS2_CMD_PORT     0x64u

/* Status register bits */
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL  0x02u
#define PS2_STATUS_AUX_DATA    0x20u

/* Controller commands */
#define PS2_CMD_WRITE_AUX      0xD4u
#define PS2_CMD_ENABLE_AUX     0xA8u
#define PS2_CMD_READ_CONFIG    0x20u
#define PS2_CMD_WRITE_CONFIG   0x60u

/* Mouse commands */
#define MOUSE_CMD_RESET        0xFFu
#define MOUSE_CMD_SET_DEFAULTS 0xF6u
#define MOUSE_CMD_ENABLE       0xF4u
#define MOUSE_CMD_SET_RATE     0xF3u

/* Mouse response codes */
#define MOUSE_ACK              0xFAu

static int g_mouse_initialized;
static unsigned char g_packet[3];
static int g_packet_index;

static inline void ps2_outb(unsigned short port, unsigned char value) {
  __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char ps2_inb(unsigned short port) {
  unsigned char value;
  __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

static void ps2_wait_input(void) {
  int timeout = 100000;
  while ((ps2_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) && --timeout > 0) {
    /* spin */
  }
}

static void ps2_wait_output(void) {
  int timeout = 100000;
  while (!(ps2_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && --timeout > 0) {
    /* spin */
  }
}

static void ps2_send_command(unsigned char cmd) {
  ps2_wait_input();
  ps2_outb(PS2_CMD_PORT, cmd);
}

static void ps2_send_mouse_command(unsigned char cmd) {
  ps2_wait_input();
  ps2_outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
  ps2_wait_input();
  ps2_outb(PS2_DATA_PORT, cmd);
}

static int ps2_read_ack(void) {
  ps2_wait_output();
  unsigned char resp = ps2_inb(PS2_DATA_PORT);
  return (resp == MOUSE_ACK) ? 1 : 0;
}

int ps2_mouse_init(void) {
  g_mouse_initialized = 0;
  g_packet_index = 0;

  /* Enable the auxiliary (mouse) port */
  ps2_send_command(PS2_CMD_ENABLE_AUX);

  /* Read current controller config */
  ps2_send_command(PS2_CMD_READ_CONFIG);
  ps2_wait_output();
  unsigned char config = ps2_inb(PS2_DATA_PORT);

  /* Enable auxiliary interrupt bit and disable auxiliary clock disable */
  config |= 0x02u;   /* Enable IRQ12 (aux interrupt) */
  config &= ~0x20u;  /* Clear auxiliary clock disable */

  ps2_send_command(PS2_CMD_WRITE_CONFIG);
  ps2_wait_input();
  ps2_outb(PS2_DATA_PORT, config);

  /* Reset the mouse */
  ps2_send_mouse_command(MOUSE_CMD_RESET);
  ps2_wait_output();
  unsigned char ack = ps2_inb(PS2_DATA_PORT);
  if (ack != MOUSE_ACK) {
    return 0;
  }

  /* Wait for self-test pass (0xAA) and device ID (0x00) */
  ps2_wait_output();
  unsigned char selftest = ps2_inb(PS2_DATA_PORT);
  if (selftest != 0xAAu) {
    return 0;
  }
  ps2_wait_output();
  (void)ps2_inb(PS2_DATA_PORT); /* device ID = 0x00 */

  /* Set defaults */
  ps2_send_mouse_command(MOUSE_CMD_SET_DEFAULTS);
  if (!ps2_read_ack()) {
    return 0;
  }

  /* Set sample rate to 100 samples/sec for reasonable responsiveness */
  ps2_send_mouse_command(MOUSE_CMD_SET_RATE);
  if (!ps2_read_ack()) {
    return 0;
  }
  ps2_send_mouse_command(100);
  if (!ps2_read_ack()) {
    return 0;
  }

  /* Enable data streaming */
  ps2_send_mouse_command(MOUSE_CMD_ENABLE);
  if (!ps2_read_ack()) {
    return 0;
  }

  g_mouse_initialized = 1;
  return 1;
}

int ps2_mouse_poll(ps2_mouse_event_t *out_event) {
  if (!g_mouse_initialized || out_event == 0) {
    return 0;
  }

  /* Check if data is available from the auxiliary device */
  unsigned char status = ps2_inb(PS2_STATUS_PORT);
  if (!(status & PS2_STATUS_OUTPUT_FULL)) {
    return 0;
  }

  /* Only process if it's mouse data (aux bit set) */
  if (!(status & PS2_STATUS_AUX_DATA)) {
    return 0;
  }

  unsigned char data = ps2_inb(PS2_DATA_PORT);

  /* First byte of packet must have bit 3 set (always-1 bit) */
  if (g_packet_index == 0) {
    if (!(data & 0x08u)) {
      /* Out of sync - discard and resync */
      return 0;
    }
  }

  g_packet[g_packet_index++] = data;

  /* Need all 3 bytes for a complete packet */
  if (g_packet_index < 3) {
    return 0;
  }

  g_packet_index = 0;

  /* Parse the 3-byte packet */
  unsigned char flags = g_packet[0];
  int dx = (int)g_packet[1];
  int dy = (int)g_packet[2];

  /* Apply sign extension from flags byte */
  if (flags & 0x10u) {
    dx |= 0xFFFFFF00;
  }
  if (flags & 0x20u) {
    dy |= 0xFFFFFF00;
  }

  /* Check overflow bits - discard if overflow */
  if ((flags & 0x40u) || (flags & 0x80u)) {
    return 0;
  }

  out_event->dx = dx;
  out_event->dy = -dy; /* Invert Y: PS/2 Y is positive-up, screen is positive-down */
  out_event->buttons = flags & 0x07u;

  return 1;
}
