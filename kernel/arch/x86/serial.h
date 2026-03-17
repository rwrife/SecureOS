#pragma once

/* Compatibility facade for legacy serial call sites. */
int serial_try_read_char(char *out_char);
void serial_init(void);
void serial_write(const char *s);
