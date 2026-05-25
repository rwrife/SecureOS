/**
 * @file auth_dialog.c
 * @brief Auth prompt dialog implementation for the SecureOS window manager.
 *
 * Purpose:
 *   Polls for pending auth prompts each frame. When active, renders a modal
 *   dialog showing the capability description and Allow/Deny/Always buttons.
 *   The dialog captures mouse clicks and responds to the kernel.
 *
 * Interactions:
 *   - auth_dialog.h: public API
 *   - secureos_api.h: os_auth_poll_prompt, os_auth_respond
 *   - font.h: text rendering into the dialog
 *
 * Launched by:
 *   Not standalone. Compiled into win.bin application.
 */

#include "auth_dialog.h"
#include "font.h"
#include "secureos_api.h"

/* Dialog dimensions and position (centered on 320x200 screen) */
#define DIALOG_W     220
#define DIALOG_H     80
#define DIALOG_X     ((320 - DIALOG_W) / 2)
#define DIALOG_Y     ((200 - DIALOG_H) / 2)

/* Button dimensions */
#define BTN_W        50
#define BTN_H        12
#define BTN_Y        (DIALOG_Y + DIALOG_H - BTN_H - 6)
#define BTN_ALLOW_X  (DIALOG_X + 10)
#define BTN_ALWAYS_X (DIALOG_X + 70)
#define BTN_DENY_X   (DIALOG_X + DIALOG_W - BTN_W - 10)

/* Colors */
#define COLOR_DLG_BG     8   /* dark grey */
#define COLOR_DLG_BORDER 7   /* light grey */
#define COLOR_DLG_TEXT   15  /* white */
#define COLOR_BTN_ALLOW  2   /* green */
#define COLOR_BTN_ALWAYS 3   /* cyan */
#define COLOR_BTN_DENY   4   /* red */
#define COLOR_BTN_TEXT   15  /* white */

static int g_dialog_active;
static os_auth_prompt_t g_prompt;

int auth_dialog_poll(void) {
  if (g_dialog_active) {
    return 1;
  }
  /* Check for new auth prompts */
  os_auth_prompt_t prompt;
  prompt.active = 0;
  if (os_auth_poll_prompt(&prompt) == OS_STATUS_OK && prompt.active) {
    /* Manual copy to avoid memcpy dependency */
    g_prompt.active = prompt.active;
    g_prompt.session_id = prompt.session_id;
    g_prompt.type = prompt.type;
    g_prompt.slot_index = prompt.slot_index;
    {
      int i;
      for (i = 0; i < 128; i++) {
        g_prompt.description[i] = prompt.description[i];
      }
    }
    g_dialog_active = 1;
    return 1;
  }
  return 0;
}

int auth_dialog_active(void) {
  return g_dialog_active;
}

int auth_dialog_click(int mx, int my) {
  if (!g_dialog_active) {
    return 0;
  }

  /* Check if click is within button area */
  if (my >= BTN_Y && my < BTN_Y + BTN_H) {
    if (mx >= BTN_ALLOW_X && mx < BTN_ALLOW_X + BTN_W) {
      /* Allow */
      os_auth_respond(g_prompt.slot_index, AUTH_RESP_ALLOW);
      g_dialog_active = 0;
      return 1;
    }
    if (mx >= BTN_ALWAYS_X && mx < BTN_ALWAYS_X + BTN_W) {
      /* Allow Always */
      os_auth_respond(g_prompt.slot_index, AUTH_RESP_ALLOW_ALWAYS);
      g_dialog_active = 0;
      return 1;
    }
    if (mx >= BTN_DENY_X && mx < BTN_DENY_X + BTN_W) {
      /* Deny */
      os_auth_respond(g_prompt.slot_index, AUTH_RESP_DENY);
      g_dialog_active = 0;
      return 1;
    }
  }

  /* Click is within dialog but not on a button — consume it anyway */
  if (mx >= DIALOG_X && mx < DIALOG_X + DIALOG_W &&
      my >= DIALOG_Y && my < DIALOG_Y + DIALOG_H) {
    return 1;
  }

  return 0;
}

static void dialog_fill_rect(unsigned char *buf, int screen_w,
                              int x, int y, int w, int h, unsigned char color) {
  int row, col;
  for (row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= 200) continue;
    for (col = 0; col < w; col++) {
      int px = x + col;
      if (px < 0 || px >= screen_w) continue;
      buf[py * screen_w + px] = color;
    }
  }
}

void auth_dialog_render(unsigned char *backbuffer, int screen_w, int screen_h) {
  int text_y;
  (void)screen_h;

  if (!g_dialog_active) {
    return;
  }

  /* Draw dialog background with border */
  dialog_fill_rect(backbuffer, screen_w,
                   DIALOG_X - 1, DIALOG_Y - 1,
                   DIALOG_W + 2, DIALOG_H + 2, COLOR_DLG_BORDER);
  dialog_fill_rect(backbuffer, screen_w,
                   DIALOG_X, DIALOG_Y, DIALOG_W, DIALOG_H, COLOR_DLG_BG);

  /* Title */
  font_draw_string(backbuffer, screen_w,
                   DIALOG_X + 4, DIALOG_Y + 4,
                   "Authorization Required", COLOR_DLG_TEXT);

  /* Description (truncated to fit) */
  text_y = DIALOG_Y + 16;
  {
    /* Render up to 2 lines of description */
    const char *desc = g_prompt.description;
    int col = 0;
    int line = 0;
    int max_cols = (DIALOG_W - 8) / 6;  /* 6px per char */
    int i = 0;

    while (desc[i] != '\0' && line < 3) {
      if (col >= max_cols) {
        col = 0;
        line++;
        text_y += 9;
      }
      if (line < 3) {
        font_draw_char(backbuffer, screen_w,
                       DIALOG_X + 4 + col * 6, text_y,
                       desc[i], COLOR_DLG_TEXT);
        col++;
      }
      i++;
    }
  }

  /* Allow button */
  dialog_fill_rect(backbuffer, screen_w,
                   BTN_ALLOW_X, BTN_Y, BTN_W, BTN_H, COLOR_BTN_ALLOW);
  font_draw_string(backbuffer, screen_w,
                   BTN_ALLOW_X + 4, BTN_Y + 3, "Allow", COLOR_BTN_TEXT);

  /* Always button */
  dialog_fill_rect(backbuffer, screen_w,
                   BTN_ALWAYS_X, BTN_Y, BTN_W, BTN_H, COLOR_BTN_ALWAYS);
  font_draw_string(backbuffer, screen_w,
                   BTN_ALWAYS_X + 4, BTN_Y + 3, "Always", COLOR_BTN_TEXT);

  /* Deny button */
  dialog_fill_rect(backbuffer, screen_w,
                   BTN_DENY_X, BTN_Y, BTN_W, BTN_H, COLOR_BTN_DENY);
  font_draw_string(backbuffer, screen_w,
                   BTN_DENY_X + 8, BTN_Y + 3, "Deny", COLOR_BTN_TEXT);
}
