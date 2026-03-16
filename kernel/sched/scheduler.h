#ifndef SECUREOS_SCHEDULER_H
#define SECUREOS_SCHEDULER_H

#include <stddef.h>

typedef void (*sched_task_entry_fn)(void *context);

typedef enum {
  SCHED_TASK_FREE = 0,
  SCHED_TASK_READY = 1,
  SCHED_TASK_RUNNING = 2,
  SCHED_TASK_DONE = 3,
} sched_task_state_t;

enum {
  SCHED_MAX_TASKS = 8,
};

void sched_init(void);
int sched_spawn(const char *name, sched_task_entry_fn entry, void *context);
int sched_run_next(void);
void sched_run_forever(void);

size_t sched_task_count_for_tests(void);
sched_task_state_t sched_task_state_for_tests(int task_id);

#endif
