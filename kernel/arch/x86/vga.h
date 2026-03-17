#pragma once

/* Compatibility facade for legacy VGA call sites. */
void vga_clear(void);
void vga_write(const char *s);
