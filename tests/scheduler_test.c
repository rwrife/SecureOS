/**
 * @file scheduler_test.c
 * @brief Tests for the cooperative task scheduler.
 *
 * Purpose:
 *   Validates task spawning, round-robin execution, task-state
 *   transitions (ready → running → done), and the absence of
 *   runnable tasks after all have completed.
 *
 * Interactions:
 *   - scheduler.c: exercises sched_init, sched_spawn, sched_run_next,
 *     and test query helpers.
 *
 * Launched by:
 *   Compiled and run by the test harness
 *   (build/scripts/test_scheduler.sh).
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/sched/scheduler.h"

static int g_counter = 0;

static void fail(const char *reason) {
  printf("TEST:FAIL:scheduler:%s\n", reason);
  exit(1);
}

static void task_inc(void *context) {
  int value = *(int *)context;
  g_counter += value;
}

int main(void) {
  int one = 1;
  int two = 2;
  int task_a = -1;
  int task_b = -1;

  printf("TEST:START:scheduler\n");

  sched_init();
  if (sched_task_count_for_tests() != 0u) {
    fail("initial_count_not_zero");
  }

  task_a = sched_spawn("inc1", task_inc, &one);
  task_b = sched_spawn("inc2", task_inc, &two);
  if (task_a < 0 || task_b < 0) {
    fail("spawn_failed");
  }

  if (sched_task_count_for_tests() != 2u) {
    fail("spawn_count_mismatch");
  }

  if (!sched_run_next()) {
    fail("run_next_first_failed");
  }
  if (!sched_run_next()) {
    fail("run_next_second_failed");
  }
  if (g_counter != 3) {
    fail("task_execution_mismatch");
  }

  if (sched_task_state_for_tests(task_a) != SCHED_TASK_DONE ||
      sched_task_state_for_tests(task_b) != SCHED_TASK_DONE) {
    fail("task_state_not_done");
  }

  if (sched_run_next()) {
    fail("unexpected_task_available");
  }

  printf("TEST:PASS:scheduler_run_contract\n");
  return 0;
}
