/**
 * @file proc_sched.c
 * @brief Cooperative scheduler + IPC block/wake (M1 plan #198 slice 3,
 *        issue #250).
 *
 * Purpose:
 *   Drives PCBs registered via process_create() (#224) through their
 *   READY -> RUNNING -> { BLOCKED -> READY }* -> EXITED lifecycle.
 *   The scheduler keeps a per-PCB POSIX ucontext on the host build so
 *   IPC send/recv can suspend a running PCB mid-call and resume the
 *   matching peer when the rendezvous lands — without target-specific
 *   assembly. Kernel ports supply their own ucontext-equivalent shim
 *   (out of scope for #250; see #251).
 *
 *   The scheduler is single-threaded and cooperative. Pre-emption,
 *   per-CPU runqueues, and priority bands are deferred to M2.
 *
 * Interactions:
 *   - kernel/proc/process.{c,h}: state / entry / blocked_on_port
 *     mutators (process_set_*).
 *   - kernel/ipc/ipc_ops.c: calls proc_sched_block_current_on_port()
 *     and proc_sched_wake_one_on_port() at the rendezvous boundary.
 *
 * Launched by:
 *   Compiled into the kernel image and into the
 *   build/scripts/test_proc_sched.sh host-side test binary.
 *
 * Issue: #250. Plan: plans/2026-05-20-m1-process-address-space.md.
 */

#include "proc_sched.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "address_space.h"

/* Per-PCB coroutine stack. 64 KiB matches the slice-2 default arena
 * stack window's lower bound (kernel/proc/address_space.h) and is more
 * than enough for the host-side smoke entries. */
#define PROC_SCHED_STACK_BYTES (64u * 1024u)

typedef struct {
  bool in_use;
  process_id_t pid;
  ucontext_t ctx;
  uint8_t *stack;
  bool started;
} sched_entry_t;

/* Sized to PROC_TABLE_MAX so registering every live PCB always fits.
 * PROC_TABLE_MAX is small in v0 (see kernel/proc/process.h). */
static sched_entry_t g_entries[PROC_TABLE_MAX];

/* Ready-queue: ring of slot indexes into g_entries. */
static uint16_t g_ready_ring[PROC_TABLE_MAX];
static uint32_t g_ready_head = 0u;
static uint32_t g_ready_count = 0u;

static ucontext_t g_scheduler_ctx;
static int16_t g_current_index = -1;  /* index into g_entries, -1 = none */
static bool g_dispatching = false;

/* Panic hook for aspace-invariant violations (issue #260). NULL in the
 * default kernel/host build; tests install a hook so they can observe
 * the violation deterministically without aborting the test process. */
static proc_sched_panic_fn_t g_aspace_panic_hook = NULL;

/* ------------------------------------------------------------------ */

static sched_entry_t *find_entry(process_id_t pid) {
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (g_entries[i].in_use && g_entries[i].pid == pid) {
      return &g_entries[i];
    }
  }
  return NULL;
}

static int16_t alloc_entry(process_id_t pid) {
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (!g_entries[i].in_use) {
      g_entries[i].in_use = true;
      g_entries[i].pid = pid;
      g_entries[i].started = false;
      g_entries[i].stack = (uint8_t *)malloc(PROC_SCHED_STACK_BYTES);
      if (g_entries[i].stack == NULL) {
        g_entries[i].in_use = false;
        return -1;
      }
      return (int16_t)i;
    }
  }
  return -1;
}

static void ready_push(uint16_t entry_index) {
  /* Caller guarantees we never exceed PROC_TABLE_MAX entries. */
  uint32_t tail = (g_ready_head + g_ready_count) % PROC_TABLE_MAX;
  g_ready_ring[tail] = entry_index;
  g_ready_count++;
}

static int16_t ready_pop(void) {
  if (g_ready_count == 0u) {
    return -1;
  }
  uint16_t idx = g_ready_ring[g_ready_head];
  g_ready_head = (g_ready_head + 1u) % PROC_TABLE_MAX;
  g_ready_count--;
  return (int16_t)idx;
}

/* Drop a ready-queue entry by entry_index. Used when a PCB exits
 * while still on the ring (shouldn't happen — exit transitions out of
 * READY first — but guards against double-enqueue bugs). */
static void ready_drop(uint16_t entry_index) {
  uint32_t kept = 0u;
  uint16_t tmp[PROC_TABLE_MAX];
  for (uint32_t i = 0; i < g_ready_count; ++i) {
    uint16_t v = g_ready_ring[(g_ready_head + i) % PROC_TABLE_MAX];
    if (v != entry_index) {
      tmp[kept++] = v;
    }
  }
  for (uint32_t i = 0; i < kept; ++i) {
    g_ready_ring[i] = tmp[i];
  }
  g_ready_head = 0u;
  g_ready_count = kept;
}

/* ------------------------------------------------------------------ */

proc_sched_panic_fn_t proc_sched_set_panic_hook_for_tests(
    proc_sched_panic_fn_t hook) {
  proc_sched_panic_fn_t prev = g_aspace_panic_hook;
  g_aspace_panic_hook = hook;
  return prev;
}

/* Categorise an aspace-invariant violation into a short, deterministic
 * reason string. Matches `aspace_invariant_ok()` semantics. */
static const char *aspace_invariant_reason(const address_space_t *as) {
  if (as == NULL) return "null_aspace";
  if (as->size == 0u) return "zero_size";
  if (as->size > (size_t)(UINTPTR_MAX - as->base)) return "window_overflow";
  uintptr_t end = as->base + (uintptr_t)as->size;
  if (as->stack_top <= as->base) return "stack_top_at_or_below_base";
  if (as->stack_top > end) return "stack_top_escapes_window";
  /* If reached, ipc_scratch must be the offender (when non-NULL). */
  return "ipc_scratch_escapes_window";
}

/* Report an invariant violation. With a test hook installed, dispatch
 * to the hook and let the scheduler force-exit the offending PCB.
 * Otherwise emit a deterministic PANIC marker and abort, matching the
 * kernel-side panic semantics. */
static void aspace_invariant_panic(process_id_t pid,
                                   const address_space_t *as) {
  const char *reason = aspace_invariant_reason(as);
  if (g_aspace_panic_hook != NULL) {
    g_aspace_panic_hook(pid, reason);
    return;
  }
  fprintf(stderr, "PANIC:proc_sched:aspace_invariant:%u:%s\n",
          (unsigned)pid, reason);
  fflush(stderr);
  abort();
}

void proc_sched_reset(void) {
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (g_entries[i].in_use && g_entries[i].stack != NULL) {
      free(g_entries[i].stack);
    }
    g_entries[i].in_use = false;
    g_entries[i].pid = PID_INVALID;
    g_entries[i].stack = NULL;
    g_entries[i].started = false;
    memset(&g_entries[i].ctx, 0, sizeof(g_entries[i].ctx));
  }
  g_ready_head = 0u;
  g_ready_count = 0u;
  g_current_index = -1;
  g_dispatching = false;
  memset(&g_scheduler_ctx, 0, sizeof(g_scheduler_ctx));
}

/* Trampoline invoked once when the scheduler first dispatches a PCB.
 * Implicit proc_exit(0) on natural return. */
static void sched_trampoline(void) {
  /* g_current_index is set to this entry by the dispatch loop before
   * swapcontext()'ing into us. */
  if (g_current_index < 0) {
    /* Defensive — should be unreachable. */
    return;
  }
  sched_entry_t *e = &g_entries[g_current_index];
  process_t snap;
  if (process_lookup(e->pid, &snap) == PROC_OK && snap.entry != NULL) {
    snap.entry();
  }
  /* Implicit exit on natural return. */
  (void)proc_exit(0u);
  /* proc_exit() does not return; if it did, fall back to scheduler. */
  swapcontext(&e->ctx, &g_scheduler_ctx);
}

proc_sched_result_t proc_sched_register(process_id_t pid,
                                        proc_entry_fn_t entry) {
  if (entry == NULL) {
    return PROC_SCHED_ERR_INVALID_ARG;
  }
  if (!process_is_live_for_tests(pid)) {
    return PROC_SCHED_ERR_INVALID_PID;
  }
  if (find_entry(pid) != NULL) {
    return PROC_SCHED_ERR_INVALID_PID;
  }
  int16_t idx = alloc_entry(pid);
  if (idx < 0) {
    return PROC_SCHED_ERR_INVALID_ARG;
  }
  if (process_set_entry(pid, entry) != PROC_OK) {
    /* Should not happen — PCB was live one line ago. */
    g_entries[idx].in_use = false;
    free(g_entries[idx].stack);
    g_entries[idx].stack = NULL;
    return PROC_SCHED_ERR_INVALID_PID;
  }
  (void)process_set_state(pid, PROC_STATE_READY);
  ready_push((uint16_t)idx);
  return PROC_SCHED_OK;
}

/* Switch from currently-running PCB back to the scheduler. */
static void switch_to_scheduler(void) {
  int16_t prev = g_current_index;
  g_current_index = -1;
  if (prev >= 0) {
    swapcontext(&g_entries[prev].ctx, &g_scheduler_ctx);
  }
}

proc_sched_result_t proc_yield(void) {
  if (g_current_index < 0) {
    return PROC_SCHED_ERR_NOT_RUNNING;
  }
  sched_entry_t *e = &g_entries[g_current_index];
  /* Flip RUNNING -> READY and re-queue at the tail for round-robin. */
  (void)process_set_state(e->pid, PROC_STATE_READY);
  ready_push((uint16_t)g_current_index);
  switch_to_scheduler();
  /* When we resume, the dispatch loop has flipped us back to RUNNING. */
  return PROC_SCHED_OK;
}

proc_sched_result_t proc_exit(uint32_t code) {
  if (g_current_index < 0) {
    return PROC_SCHED_ERR_NOT_RUNNING;
  }
  sched_entry_t *e = &g_entries[g_current_index];
  (void)process_set_exit_code(e->pid, code);
  (void)process_set_state(e->pid, PROC_STATE_EXITED);
  /* Defensive: ensure we are not on the ready ring. */
  ready_drop((uint16_t)g_current_index);
  switch_to_scheduler();
  /* Unreachable in practice — the scheduler never re-dispatches an
   * EXITED PCB. */
  return PROC_SCHED_OK;
}

proc_sched_result_t proc_sched_block_current_on_port(const void *port) {
  if (port == NULL) {
    return PROC_SCHED_ERR_INVALID_ARG;
  }
  if (g_current_index < 0) {
    return PROC_SCHED_ERR_NOT_RUNNING;
  }
  sched_entry_t *e = &g_entries[g_current_index];
  (void)process_set_blocked_on(e->pid, port);
  (void)process_set_state(e->pid, PROC_STATE_BLOCKED);
  switch_to_scheduler();
  /* Resumed: scheduler will have cleared blocked_on_port and flipped
   * state to RUNNING. */
  return PROC_SCHED_OK;
}

proc_sched_result_t proc_sched_wake_one_on_port(const void *port) {
  if (port == NULL) {
    return PROC_SCHED_ERR_INVALID_ARG;
  }
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (!g_entries[i].in_use) {
      continue;
    }
    process_t snap;
    if (process_lookup(g_entries[i].pid, &snap) != PROC_OK) {
      continue;
    }
    if (snap.state == PROC_STATE_BLOCKED && snap.blocked_on_port == port) {
      (void)process_set_blocked_on(g_entries[i].pid, NULL);
      (void)process_set_state(g_entries[i].pid, PROC_STATE_READY);
      ready_push(i);
      return PROC_SCHED_OK;
    }
  }
  return PROC_SCHED_ERR_NOT_BLOCKED;
}

/* Count blocked PCBs by walking the entry table. */
static uint32_t count_blocked(void) {
  uint32_t n = 0;
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (!g_entries[i].in_use) continue;
    process_t snap;
    if (process_lookup(g_entries[i].pid, &snap) != PROC_OK) continue;
    if (snap.state == PROC_STATE_BLOCKED) n++;
  }
  return n;
}

static uint32_t count_live(void) {
  uint32_t n = 0;
  for (uint16_t i = 0; i < PROC_TABLE_MAX; ++i) {
    if (!g_entries[i].in_use) continue;
    process_t snap;
    if (process_lookup(g_entries[i].pid, &snap) != PROC_OK) continue;
    if (snap.state != PROC_STATE_EXITED) n++;
  }
  return n;
}

proc_sched_result_t proc_sched_run(void) {
  if (g_dispatching) {
    return PROC_SCHED_ERR_INVALID_ARG;
  }
  g_dispatching = true;

  for (;;) {
    int16_t idx = ready_pop();
    if (idx < 0) {
      /* No ready PCBs. If any PCB is still BLOCKED we have a
       * deadlock (no waker possible in v0). Otherwise orderly drain. */
      uint32_t live = count_live();
      g_dispatching = false;
      if (live == 0u) {
        return PROC_SCHED_OK;
      }
      return PROC_SCHED_ERR_DEADLOCK;
    }
    sched_entry_t *e = &g_entries[idx];
    process_t snap;
    if (process_lookup(e->pid, &snap) != PROC_OK ||
        snap.state == PROC_STATE_EXITED) {
      /* Stale entry — skip. */
      continue;
    }

    /* Address-space invariant check (issue #260 done-when 3). Every
     * PCB whose context is about to be restored must satisfy
     * `aspace_invariant_ok(pcb->aspace)`; a `false` return means the
     * scheduler has been handed a corrupted window and continuing
     * would silently restore an out-of-bounds stack pointer. Panic
     * (or hook into the test harness) before swapping context. */
    if (!aspace_invariant_ok(snap.aspace)) {
      aspace_invariant_panic(e->pid, snap.aspace);
      /* If we reach here a test hook absorbed the violation. Force
       * the offending PCB to EXITED with a sentinel exit_code so the
       * dispatch loop keeps making forward progress. */
      (void)process_set_exit_code(e->pid, UINT32_MAX);
      (void)process_set_state(e->pid, PROC_STATE_EXITED);
      ready_drop((uint16_t)idx);
      g_current_index = -1;
      continue;
    }

    g_current_index = idx;
    (void)process_set_state(e->pid, PROC_STATE_RUNNING);

    if (!e->started) {
      e->started = true;
      /* First dispatch: build the coroutine context. */
      if (getcontext(&e->ctx) != 0) {
        /* Treat as a fatal scheduler error. */
        (void)process_set_state(e->pid, PROC_STATE_EXITED);
        g_current_index = -1;
        continue;
      }
      e->ctx.uc_stack.ss_sp = e->stack;
      e->ctx.uc_stack.ss_size = PROC_SCHED_STACK_BYTES;
      e->ctx.uc_link = &g_scheduler_ctx;
      makecontext(&e->ctx, sched_trampoline, 0);
    }
    swapcontext(&g_scheduler_ctx, &e->ctx);
    /* On return, g_current_index has been cleared by switch_to_scheduler. */
    g_current_index = -1;
  }
}

/* ---------------- test-only inspectors ---------------- */

process_id_t proc_sched_current_for_tests(void) {
  if (g_current_index < 0) return PID_INVALID;
  return g_entries[g_current_index].pid;
}

uint32_t proc_sched_ready_count_for_tests(void) {
  return g_ready_count;
}

uint32_t proc_sched_blocked_count_for_tests(void) {
  return count_blocked();
}

bool proc_sched_is_active(void) {
  return g_dispatching && g_current_index >= 0;
}
