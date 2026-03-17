#ifndef SECUREOS_FRAMEBUFFER_TEXT_STUB_H
#define SECUREOS_FRAMEBUFFER_TEXT_STUB_H

/**
 * @file framebuffer_text_stub.h
 * @brief Stub framebuffer text backend registration helpers.
 *
 * Purpose:
 *   Declares entry points for registering a framebuffer-style text backend
 *   as the active video device in video HAL.
 *
 * Interactions:
 *   - hal/video_hal.c: receives backend registration.
 *   - core/kmain.c: may call framebuffer_text_stub_init_primary() as a
 *     fallback when VGA text backend is unavailable.
 *
 * Launched by:
 *   Called during kernel initialization; not a standalone process.
 */

int framebuffer_text_stub_init_primary(void);

#endif
