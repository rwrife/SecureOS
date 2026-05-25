/**
 * @file ps2_keyboard.c
 * @brief PS/2 keyboard driver for x86 8042 controller.
 *
 * Purpose:
 *   Implements keyboard input by polling the PS/2 keyboard controller at
 *   I/O ports 0x60 (data) and 0x64 (status). Translates scan code set 1
 *   key-press events into ASCII characters. Handles shift state for
 *   uppercase letters and shifted symbols.
 *
 * Interactions:
 *   - hal/input_hal.c: the input HAL calls ps2_keyboard_try_read_char()
 *     as one of its input sources.
 *   - core/kmain.c: calls ps2_keyboard_init() at boot time.
 *
 * Launched by:
 *   Invoked during kernel startup. Not a standalone process; compiled into
 *   the kernel image.
 */

#include "ps2_keyboard.h"

#define PS2_DATA_PORT   0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_CMD_PORT    0x64u

/* Status register bits */
#define PS2_STATUS_OUTPUT_FULL 0x01u

/* Scan code set 1 key codes */
#define SC_LSHIFT_PRESS   0x2Au
#define SC_RSHIFT_PRESS   0x36u
#define SC_LSHIFT_RELEASE 0xAAu
#define SC_RSHIFT_RELEASE 0xB6u
#define SC_CAPSLOCK_PRESS 0x3Au
#define SC_CTRL_PRESS     0x1Du
#define SC_CTRL_RELEASE   0x9Du
#define SC_BACKSPACE      0x0Eu
#define SC_ENTER          0x1Cu
#define SC_TAB            0x0Fu
#define SC_ESCAPE         0x01u
#define SC_UP_ARROW       0x48u
#define SC_DOWN_ARROW     0x50u

static int g_shift_held;
static int g_caps_lock;
static int g_ctrl_held;

/* Multi-char output buffer for escape sequences (e.g. arrow keys) */
#define PS2_SEQ_BUF_MAX 4
static char g_seq_buf[PS2_SEQ_BUF_MAX];
static int g_seq_len;
static int g_seq_pos;

static inline void ps2_outb(unsigned short port, unsigned char value) {
  __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char ps2_inb(unsigned short port) {
  unsigned char value;
  __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

/* Scan code set 1 -> ASCII lookup (unshifted) */
static const char sc_to_ascii[128] = {
  0,    0x1B, '1',  '2',  '3',  '4',  '5',  '6',   /* 0x00-0x07 */
  '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',  /* 0x08-0x0F */
  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',   /* 0x10-0x17 */
  'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',   /* 0x18-0x1F */
  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 0x20-0x27 */
  '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',   /* 0x28-0x2F */
  'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',   /* 0x30-0x37 */
  0,    ' ',  0,    0,    0,    0,    0,    0,      /* 0x38-0x3F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x40-0x47 */
  0,    0,    '-',  0,    0,    0,    '+',  0,      /* 0x48-0x4F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x50-0x57 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x58-0x5F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x60-0x67 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x68-0x6F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x70-0x77 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x78-0x7F */
};

/* Scan code set 1 -> ASCII lookup (shifted) */
static const char sc_to_ascii_shift[128] = {
  0,    0x1B, '!',  '@',  '#',  '$',  '%',  '^',   /* 0x00-0x07 */
  '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',  /* 0x08-0x0F */
  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',   /* 0x10-0x17 */
  'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',   /* 0x18-0x1F */
  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',   /* 0x20-0x27 */
  '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',   /* 0x28-0x2F */
  'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',   /* 0x30-0x37 */
  0,    ' ',  0,    0,    0,    0,    0,    0,      /* 0x38-0x3F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x40-0x47 */
  0,    0,    '-',  0,    0,    0,    '+',  0,      /* 0x48-0x4F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x50-0x57 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x58-0x5F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x60-0x67 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x68-0x6F */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x70-0x77 */
  0,    0,    0,    0,    0,    0,    0,    0,      /* 0x78-0x7F */
};

int ps2_keyboard_init(void) {
  g_shift_held = 0;
  g_caps_lock = 0;
  g_ctrl_held = 0;
  g_seq_len = 0;
  g_seq_pos = 0;

  /* Flush any pending data in the controller output buffer */
  int flush_count = 0;
  while ((ps2_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) &&
         flush_count < 64) {
    (void)ps2_inb(PS2_DATA_PORT);
    flush_count++;
  }

  return 1;
}

static void ps2_queue_sequence(const char *seq, int len) {
  int i;
  for (i = 0; i < len && i < PS2_SEQ_BUF_MAX; i++) {
    g_seq_buf[i] = seq[i];
  }
  g_seq_len = len;
  g_seq_pos = 0;
}

int ps2_keyboard_try_read_char(char *out_char) {
  if (out_char == 0) {
    return 0;
  }

  /* Drain any pending multi-char sequence first */
  if (g_seq_pos < g_seq_len) {
    *out_char = g_seq_buf[g_seq_pos++];
    return 1;
  }

  /* Check if data is available from the keyboard (not mouse) */
  unsigned char status = ps2_inb(PS2_STATUS_PORT);
  if (!(status & PS2_STATUS_OUTPUT_FULL)) {
    return 0;
  }
  /* Bit 5 (0x20) indicates auxiliary device (mouse) data - skip it.
   * The mouse driver (mouse_hal_update) is responsible for draining
   * mouse bytes from the port. */
  if (status & 0x20u) {
    return 0;
  }

  unsigned char scancode = ps2_inb(PS2_DATA_PORT);

  /* Handle modifier keys */
  if (scancode == SC_LSHIFT_PRESS || scancode == SC_RSHIFT_PRESS) {
    g_shift_held = 1;
    return 0;
  }
  if (scancode == SC_LSHIFT_RELEASE || scancode == SC_RSHIFT_RELEASE) {
    g_shift_held = 0;
    return 0;
  }
  if (scancode == SC_CTRL_PRESS) {
    g_ctrl_held = 1;
    return 0;
  }
  if (scancode == SC_CTRL_RELEASE) {
    g_ctrl_held = 0;
    return 0;
  }
  if (scancode == SC_CAPSLOCK_PRESS) {
    g_caps_lock = !g_caps_lock;
    return 0;
  }

  /* Ignore key release events (bit 7 set) */
  if (scancode & 0x80u) {
    return 0;
  }

  /* Handle arrow keys - emit ANSI escape sequences */
  if (scancode == SC_UP_ARROW) {
    ps2_queue_sequence("\x1B[A", 3);
    *out_char = g_seq_buf[g_seq_pos++];
    return 1;
  }
  if (scancode == SC_DOWN_ARROW) {
    ps2_queue_sequence("\x1B[B", 3);
    *out_char = g_seq_buf[g_seq_pos++];
    return 1;
  }

  /* Translate scancode to ASCII */
  char ch;
  int use_shift = g_shift_held;

  if (use_shift) {
    ch = sc_to_ascii_shift[scancode];
  } else {
    ch = sc_to_ascii[scancode];
  }

  /* Apply caps lock to letters */
  if (g_caps_lock && ch >= 'a' && ch <= 'z') {
    ch = ch - 'a' + 'A';
  } else if (g_caps_lock && ch >= 'A' && ch <= 'Z' && !g_shift_held) {
    ch = ch - 'A' + 'a';
  }

  /* Handle Ctrl+key combinations */
  if (g_ctrl_held && ch >= 'a' && ch <= 'z') {
    ch = (char)(ch - 'a' + 1);
  } else if (g_ctrl_held && ch >= 'A' && ch <= 'Z') {
    ch = (char)(ch - 'A' + 1);
  }

  if (ch == 0) {
    return 0;
  }

  *out_char = ch;
  return 1;
}
