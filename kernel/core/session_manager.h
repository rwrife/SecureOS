#ifndef SECUREOS_SESSION_MANAGER_H
#define SECUREOS_SESSION_MANAGER_H

#include <stddef.h>

#include "../cap/capability.h"

void session_manager_start(cap_subject_id_t bootstrap_subject_id);
int session_manager_create(cap_subject_id_t subject_id, unsigned int *out_session_id);
int session_manager_switch(unsigned int session_id);
unsigned int session_manager_active_id(void);
size_t session_manager_list(char *out_buffer, size_t out_buffer_size);

#endif
