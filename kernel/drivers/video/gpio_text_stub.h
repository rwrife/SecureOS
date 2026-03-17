#ifndef SECUREOS_GPIO_TEXT_STUB_H
#define SECUREOS_GPIO_TEXT_STUB_H

/**
 * @file gpio_text_stub.h
 * @brief Stub GPIO text backend registration helpers.
 *
 * Purpose:
 *   Declares registration entry points for a GPIO-oriented text output stub
 *   backend in video HAL.
 *
 * Interactions:
 *   - hal/video_hal.c: receives backend registration.
 *   - core/kmain.c: may use gpio_text_stub_init_primary() as final fallback
 *     for minimal hardware configurations.
 *
 * Launched by:
 *   Called during kernel initialization; not a standalone process.
 */

int gpio_text_stub_init_primary(void);

#endif
