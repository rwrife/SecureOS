#ifndef SECUREOS_CONSOLE_H
#define SECUREOS_CONSOLE_H

#include <stddef.h>

#include "../cap/capability.h"

void console_init(cap_subject_id_t subject_id);
void console_run(void);

#endif
