#include "cap_gate.h"

#include <string.h>

static cap_result_t cap_write_gate(cap_subject_id_t subject_id,
                                   capability_id_t required_capability,
                                   const char *message,
                                   size_t *bytes_written) {
  cap_result_t check_result = cap_check(subject_id, required_capability);
  if (check_result != CAP_OK) {
    return check_result;
  }

  if (bytes_written != NULL) {
    if (message == NULL) {
      *bytes_written = 0u;
    } else {
      *bytes_written = strlen(message);
    }
  }

  return CAP_OK;
}

cap_result_t cap_console_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written) {
  return cap_write_gate(subject_id, CAP_CONSOLE_WRITE, message, bytes_written);
}

cap_result_t cap_serial_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written) {
  return cap_write_gate(subject_id, CAP_SERIAL_WRITE, message, bytes_written);
}
