#ifndef SECUREOS_CAP_GATE_H
#define SECUREOS_CAP_GATE_H

#include <stddef.h>

#include "capability.h"

cap_result_t cap_console_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written);
cap_result_t cap_serial_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written);

#endif