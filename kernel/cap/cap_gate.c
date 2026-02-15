#include "cap_gate.h"

#include <string.h>

cap_result_t cap_console_write_gate(cap_subject_id_t subject_id, const char *message, size_t *bytes_written) {
  cap_result_t check_result = cap_check(subject_id, CAP_CONSOLE_WRITE);
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
