/**
 * @file scheduler.c
 * @brief Cooperative round-robin task scheduler.
 *
 * Purpose:
 *   Implements a lightweight cooperative scheduler that maintains a
 *   fixed-size table of named tasks.  Each task has an entry function
 *   and context pointer.  The scheduler runs tasks in round-robin order
 *   and provides the main execution loop for the kernel after all
 *   subsystems are initialized.
 *
 * Interactions:
 *   - session_manager.c: spawns per-session console tasks via
 *     sched_spawn.
 *   - kmain.c: sched_init() is called during boot; sched_run_forever()
 *     is entered via the session manager to begin task dispatch.
 *
 * Launched by:
 *   sched_init() is called from kmain() during kernel boot.
 *   sched_run_forever() is the terminal call that never returns.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "scheduler.h"

enum {
  SCHED_NAME_MAX = 24,
};

typedef struct {
  sched_task_state_t state;
  sched_task_entry_fn entry;
  void *context;
  char name[SCHED_NAME_MAX];
} sched_task_t;

static sched_task_t g_tasks[SCHED_MAX_TASKS];
static int g_last_task_index = -1;

static void sched_copy_name(char *dst, size_t dst_size, const char *src) {
  size_t i = 0u;

  if (dst == 0 || dst_size == 0u) {
    return;
  }

  if (src == 0) {
    dst[0] = '\0';
    return;
  }

  while (src[i] != '\0' && i + 1u < dst_size) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = '\0';
}

void sched_init(void) {
  size_t i = 0u;

  g_last_task_index = -1;
  for (i = 0u; i < SCHED_MAX_TASKS; ++i) {
    g_tasks[i].state = SCHED_TASK_FREE;
    g_tasks[i].entry = 0;
    g_tasks[i].context = 0;
    g_tasks[i].name[0] = '\0';
  }
}

int sched_spawn(const char *name, sched_task_entry_fn entry, void *context) {
  size_t i = 0u;

  if (entry == 0) {
    return -1;
  }

  for (i = 0u; i < SCHED_MAX_TASKS; ++i) {
    if (g_tasks[i].state == SCHED_TASK_FREE || g_tasks[i].state == SCHED_TASK_DONE) {
      g_tasks[i].state = SCHED_TASK_READY;
      g_tasks[i].entry = entry;
      g_tasks[i].context = context;
      sched_copy_name(g_tasks[i].name, sizeof(g_tasks[i].name), name);
      return (int)i;
    }
  }

  return -1;
}

int sched_run_next(void) {
  int checked = 0;
  int index = g_last_task_index;

  while (checked < SCHED_MAX_TASKS) {
    index = (index + 1) % SCHED_MAX_TASKS;
    if (g_tasks[index].state == SCHED_TASK_READY) {
      g_tasks[index].state = SCHED_TASK_RUNNING;
      g_last_task_index = index;
      g_tasks[index].entry(g_tasks[index].context);
      if (g_tasks[index].state == SCHED_TASK_RUNNING) {
        g_tasks[index].state = SCHED_TASK_DONE;
      }
      return 1;
    }
    ++checked;
  }

  return 0;
}

void sched_run_forever(void) {
  for (;;) {
    volatile int spin = 0;
    if (!sched_run_next()) {
      spin++;
    }
  }
}

size_t sched_task_count_for_tests(void) {
  size_t i = 0u;
  size_t count = 0u;

  for (i = 0u; i < SCHED_MAX_TASKS; ++i) {
    if (g_tasks[i].state != SCHED_TASK_FREE) {
      ++count;
    }
  }

  return count;
}

sched_task_state_t sched_task_state_for_tests(int task_id) {
  if (task_id < 0 || task_id >= SCHED_MAX_TASKS) {
    return SCHED_TASK_FREE;
  }

  return g_tasks[task_id].state;
}
