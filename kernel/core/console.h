#ifndef SECUREOS_CONSOLE_H
#define SECUREOS_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"

enum {
	CONSOLE_LINE_MAX = 128,
	CONSOLE_HISTORY_MAX = 16,
	CONSOLE_ENV_MAX = 16,
	CONSOLE_ENV_KEY_MAX = 24,
	CONSOLE_ENV_VALUE_MAX = 96,
	CONSOLE_LOADED_LIB_MAX = 8,
	CONSOLE_LIB_PATH_MAX = 64,
	CONSOLE_LIB_OWNER_ACTOR_MAX = 24,
	CONSOLE_SCREEN_HISTORY_MAX = 4096,
	CONSOLE_AUTH_CACHE_MAX = 32,
	CONSOLE_AUTH_CACHE_KEY_MAX = 128,
};

typedef struct {
	int used;
	char key[CONSOLE_ENV_KEY_MAX];
	char value[CONSOLE_ENV_VALUE_MAX];
} console_env_entry_t;

typedef struct {
	int used;
	unsigned int handle;
	size_t program_len;
	unsigned int ref_count;
	unsigned int owner_session_id;
	cap_subject_id_t owner_subject_id;
	char owner_actor[CONSOLE_LIB_OWNER_ACTOR_MAX];
	char path[CONSOLE_LIB_PATH_MAX];
} console_loaded_lib_entry_t;

typedef enum {
	AUTH_CACHE_ALLOW = 1,
	AUTH_CACHE_DENY = 2,
} auth_cache_decision_t;

typedef struct {
	int used;
	char key[CONSOLE_AUTH_CACHE_KEY_MAX];
	auth_cache_decision_t decision;
} console_auth_cache_entry_t;

typedef struct {
	cap_subject_id_t subject_id;
	char line[CONSOLE_LINE_MAX];
	size_t line_len;
	char pending_line[CONSOLE_LINE_MAX];
	size_t pending_line_len;
	char history[CONSOLE_HISTORY_MAX][CONSOLE_LINE_MAX];
	size_t history_count;
	size_t history_next;
	int history_browse_index;
	char screen_history[CONSOLE_SCREEN_HISTORY_MAX];
	size_t screen_history_len;
	uint64_t next_correlation_id;
	char cwd[64];
	unsigned int escape_state;
	console_env_entry_t env_entries[CONSOLE_ENV_MAX];
	unsigned int next_loaded_lib_handle;
	console_loaded_lib_entry_t loaded_libs[CONSOLE_LOADED_LIB_MAX];
	console_auth_cache_entry_t auth_cache[CONSOLE_AUTH_CACHE_MAX];
} console_context_t;

void console_init(console_context_t *context, cap_subject_id_t subject_id);
void console_bind_context(console_context_t *context);
void console_run(void);

#endif
