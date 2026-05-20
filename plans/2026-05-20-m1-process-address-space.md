# 2026-05-20 M1 Process Abstraction + Address-Space Boundary (Plan)

**Status:** Plan-only (per #192). Implementation deferred to follow-up execute issues enumerated below.
**Tracks:** BUILD_ROADMAP §5.1 (M1 minimal kernel isolation + IPC skeleton), deliverable #1 of 3.
**Owner:** kernel
**Last reviewed:** 2026-05-20
**Related:** #180 / `plans/2026-05-19-m1-sync-ipc-primitive.md` (M1 sync IPC primitive), #193 (M1 kernel capability table skeleton), #163 / `docs/architecture/kernel-module-boundaries.md` (layer model), #93 / #150 / #181 (ABI surface), #116 (boot protocol ADR), #164 (capability-denied marker), #149 (plan directory drift).

## Motivation

BUILD_ROADMAP §5.1 ("M1: Minimal kernel isolation + IPC skeleton") lists three
deliverables: (1) **process abstraction / address-space boundary**, (2) a
synchronous IPC primitive, (3) a kernel capability table skeleton. Only #2 has
a plan today (`plans/2026-05-19-m1-sync-ipc-primitive.md`, #180) and #3 is
planned in parallel (#193). Without #1, IPC and the capability table have
nothing to isolate *between*: the M1 acceptance demo ("two modules exchange a
message; unauthorized op denied with explicit error") is meaningless unless
those "two modules" sit on opposite sides of a real boundary.

The repo today has `kernel/user/process.c`, but it is a **command/ELF
launcher** that runs in-kernel on behalf of the console — there is no separate
address space, no PCB lifecycle, no ring-3 transition, and no per-process
isolation. M1 needs the smallest viable structure that lets two modules be
named, scheduled (cooperatively), and isolated enough that an unauthorized IPC
op can be denied at a real trust boundary.

This plan is intentionally narrow: pick one address-space representation,
define a PCB shape and lifecycle, and enumerate concrete follow-up execute
issues. No code lands in this issue.

## Non-goals

- Demand paging, copy-on-write, swap, or any virtual-memory feature beyond
  static per-process mappings established at load time.
- Preemptive scheduling. M1 uses **cooperative** scheduling driven by IPC
  blocking points (see §IPC integration). A timer-driven scheduler is M2+.
- SMP / multi-CPU. M1 assumes one logical CPU; per-CPU state lives in a single
  static struct.
- User-space threads, signals, or `fork`/`exec` semantics.
- Loading processes from a real filesystem (M3). M1 modules are statically
  linked into the kernel image and "spawned" by name from a built-in registry,
  matching how the IPC plan already describes "two in-kernel test modules".
- A new ELF loader or relocation engine. Process code is the existing module's
  entry-point function pointer.
- Killing / reaping in any sophisticated way beyond `EXITED` lifecycle state.
- Standard C library, heap allocator inside the address space, or any user-mode
  syscall stub library (deferred to M6 / #136).

## Design surface

### What "process" means in M1

A **process** is the runtime instance of a registered kernel-side module,
identified by a stable `process_id_t` and bound 1:1 to a `cap_subject_id_t`
(the existing capability identity already consumed by `kernel/cap/`). The
process is the unit the capability table grants caps to and the unit IPC
endpoints (#180) are owned by.

Proposed PCB (`kernel/proc/process.{c,h}`, new directory — `kernel/user/`
stays the *command launcher* and is explicitly out of scope for renaming in
this slice; rename/move is tracked separately to keep this PR small):

```
typedef uint16_t process_id_t;   /* 0 = invalid, 1..N reserved */

typedef enum {
  PROC_STATE_NEW,        /* PCB allocated, not yet started */
  PROC_STATE_READY,      /* runnable, waiting to be scheduled */
  PROC_STATE_RUNNING,    /* currently executing on the CPU */
  PROC_STATE_BLOCKED,    /* waiting on IPC (see ipc_port.recv/send) */
  PROC_STATE_EXITED      /* terminated; PCB retained for audit */
} process_state_t;

typedef struct process {
  process_id_t       pid;
  cap_subject_id_t   subject;       /* identity used for cap_gate / audit */
  process_state_t    state;
  const char        *name;          /* static string from module registry */
  void             (*entry)(void);  /* module entry, called once on first run */
  address_space_t   *aspace;        /* see below; non-NULL once started */
  uint32_t           exit_code;     /* valid in EXITED */
  /* cooperative scheduler bookkeeping; opaque to callers */
  struct process    *next_ready;
  struct process    *blocked_on_port; /* ipc_port_t back-ref or NULL */
} process_t;
```

PCB table is a fixed-size static array (`PROC_MAX = 8` in M1; raise later).
No dynamic allocation in the kernel for M1.

### Address-space representation: choice and justification

**Decision: flat-with-bounds in M1.** Each process gets an `address_space_t`
that owns:

- a single contiguous `[base, base + size)` window carved out of a
  kernel-reserved arena at boot,
- a per-process kernel stack (fixed 16 KiB),
- a per-process IPC envelope scratch buffer (`IPC_MSG_PAYLOAD_MAX` from #180).

```
typedef struct address_space {
  uintptr_t  base;
  size_t     size;
  uintptr_t  stack_top;     /* top of per-process kernel stack */
  uint8_t   *ipc_scratch;   /* IPC_MSG_PAYLOAD_MAX bytes */
  /* page-table handle reserved for future paging-backed variant */
  void      *pt_reserved;
} address_space_t;
```

Why flat-with-bounds, not per-process page tables, for M1:

1. **Boot path stability.** The current 64-bit long-mode entry (`commit
   86c3768`, see #116 ADR draft) establishes a single kernel page-table tree
   and a flat code+data mapping. Introducing per-process page tables would
   require concurrently solving TLB invalidation, kernel-mapping inheritance,
   and IPC envelope copy-across-address-spaces — none of which §5.1 demands.
2. **§5.1's acceptance criteria are about authorization, not memory safety.**
   The two validation bullets ("two modules exchange message" + "unauthorized
   operation denied with explicit error") are satisfiable with bounds-checked
   software isolation: the IPC primitive (#180) gates send/recv through
   `cap_gate.c` regardless of whether memory is enforced by hardware.
3. **Bounded scope.** Bounds-checked windows allow `process_load`,
   `process_start`, and `process_exit` to be implemented and tested without
   touching `kernel/arch/x86/` paging code. The `pt_reserved` slot is the
   forward-compat hook for the M2/M3 transition to real page tables.
4. **Doesn't preclude the future.** The PCB+ASPACE shape above is what a real
   page-table-backed implementation also needs; only the `aspace_*` helpers
   change. Execute issues that flip to real paging are explicitly part of the
   M2 plan (out of scope here).

Trade-off acknowledged: in M1, a misbehaving module *can* technically reach
outside its window because there is no MMU enforcement. M1 mitigates this by
(a) only loading **signed, in-tree** modules from the registry, and (b)
treating any pointer crossing a process boundary as a hard kernel assertion
failure in debug builds. Any module that demonstrably needs hardware
enforcement is out-of-scope for M1 and pushes that requirement onto M2.

### Kernel/user separation strategy in M1

- **Single ring (ring 0) in M1.** Modules execute in the same privilege ring
  as the kernel because they are statically linked. A ring-3 transition
  requires GDT/TSS plumbing, a syscall entry stub, and userland startup —
  too much for the M1 slice and not required by §5.1.
- **Logical separation via the capability gate.** Every cross-module call goes
  through IPC (#180), which routes through `cap_gate.c`. A module that lacks
  the relevant cap is denied with the standard marker (`CAP:DENY:<subject>:
  <cap>`, #164) before any state mutation. This is the "boundary" for M1.
- **Syscall entry stub is stubbed but unused.** Add a single
  `kernel/proc/syscall_entry.{c,h}` placeholder that defines the planned
  ring-3 syscall ABI vector number (owned by execute issue, locked under
  `OS_ABI_VERSION` per #150) but returns `IPC_ERR_INVALID_MSG` in M1. This
  reserves the ABI slot without claiming we implement it.
- **Stack handling.** Each PCB owns a fixed 16 KiB kernel stack at
  `aspace.stack_top`; the cooperative scheduler swaps stack pointers on
  context switch. No user-mode stack in M1.

### Integration with the existing 64-bit long-mode boot path

- The arena address-spaces are carved out of in `kernel/arch/x86/boot/`
  *after* long-mode is up and the kernel BSS has been zeroed. The kernel
  reserves a fixed `PROC_ARENA_BYTES = 1 MiB` region (initially) at link time
  via a new symbol in `kernel/arch/x86/boot/linker.ld`.
- No changes to `entry.asm`. No new sections beyond a `.proc_arena` BSS
  segment.
- The boot sequence becomes:
  1. `entry.asm` → long-mode (today, unchanged).
  2. `kernel_main` (today) → existing init.
  3. **New:** `proc_init()` allocates the static PCB table and partitions
     `.proc_arena` into `PROC_MAX` windows.
  4. **New:** `proc_spawn_module(name)` is called for each registered M1
     module (the two test modules used by the IPC demo).
  5. Existing console/serial init proceeds; the console can also call
     `proc_spawn_module` for the M1 demo target.

### Two-module IPC demo (cross-ref #180)

The acceptance demo runs entirely inside the kernel image:

- `module_a` (registered as `"m1-sender"`) is spawned as `pid=1`,
  `subject=SUBJECT_M1_SENDER`. Holds `CAP_IPC_SEND` for `module_b`'s port.
- `module_b` (registered as `"m1-receiver"`) is spawned as `pid=2`,
  `subject=SUBJECT_M1_RECEIVER`. Owns an `ipc_port_t` declaring
  `recv_cap=CAP_IPC_RECV` (held by `module_b`) and `send_cap=CAP_IPC_SEND`.
- The cooperative scheduler runs `module_b` first; it calls `ipc_recv` and
  blocks (state → `BLOCKED`, `blocked_on_port` set).
- The scheduler then runs `module_a`; it calls `ipc_send` with a fixed
  payload; `cap_gate` verifies `CAP_IPC_SEND`; the envelope is copied through
  the receiver's `aspace.ipc_scratch`; `module_b` is moved back to `READY`
  and woken; `module_a` exits with `exit_code=0`.
- Deny path: a third spawn of `module_a` with `subject=SUBJECT_M1_UNAUTH`
  (intentionally lacking `CAP_IPC_SEND`) is the falsifier. The send call
  must return `IPC_ERR_CAP_DENIED`, the `CAP:DENY:m1-unauth:CAP_IPC_SEND`
  marker must be emitted, and `module_b` must remain blocked (no envelope
  delivered).

Both paths produce deterministic `TEST:PASS:m1_ipc_allow` and
`TEST:PASS:m1_ipc_deny` markers, matching the pattern #92 / #108 / #115
already established and the JSON-report contract in #110.

### Capability table interaction (cross-ref #193)

This plan does **not** define the cap table layout — that is owned by #193.
What this plan commits to is:

- Every PCB carries exactly one `cap_subject_id_t` and never mutates it
  after `PROC_STATE_NEW → READY`.
- Caps are granted at module-registry time (static table compiled into the
  kernel image, no dynamic grants in M1) so the M1 IPC demo is fully
  deterministic.
- `process_exit` calls `cap_table_revoke_subject(pid->subject)` so PCB reuse
  cannot inherit stale caps. (Even though M1 has no PCB reuse — `PROC_MAX`
  is sized to the worst case — this contract is what M2 will rely on.)

## ABI surface touched

- New header `kernel/proc/process_abi.h` declaring `process_id_t`,
  `process_state_t`, `PROC_MAX`, `PROC_ARENA_BYTES`. All values frozen under
  `OS_ABI_VERSION` (#150).
- New header `kernel/proc/address_space.h` declaring `address_space_t`.
- No additions to `docs/abi/` syscall surface in M1 — the syscall entry stub
  is reserved but not exposed to user-space yet (no user-space exists). A
  follow-up doc update will land alongside the first execute issue that does
  expose a syscall.

## Risks and explicit assumptions

- **Risk:** flat-with-bounds means a buggy module corrupts kernel memory.
  *Mitigation:* M1 modules are in-tree, code-signed by the existing
  `kernel/crypto/` chain, and the validator (`build/scripts/test.sh`) runs
  the deny-path test on every CI cycle. A regression here is caught as a
  `TEST:FAIL`, not as silent corruption, because the deny path asserts
  exactly which side effects must *not* occur.
- **Risk:** cooperative scheduling deadlocks if a module never yields.
  *Mitigation:* the IPC primitive (#180) is the *only* blocking point in M1,
  and every test module's entry function is bounded by a finite send/recv
  sequence ending in `process_exit`. Watchdog wired to `run_qemu.sh`
  (existing test harness) bounds wall-time.
- **Risk:** `kernel/user/process.c` (today's command launcher) is confused
  with the new `kernel/proc/` process abstraction.
  *Mitigation:* docs/architecture/kernel-module-boundaries.md (§5 drift
  note) gains a sentence pointing readers at `kernel/proc/` as the new home
  for the M1 PCB; the existing launcher stays put under its old name. A
  rename pass is filed as a separate housekeeping issue, **not** bundled.
- **Assumption:** #180 and #193 land before any code from this plan, because
  the PCB references both `cap_subject_id_t` (cap table) and `ipc_port_t`
  (IPC) directly. If either slips, the first execute issue below is the one
  that gates on it.

## Acceptance demo (maps directly to §5.1 validation bullets)

| Roadmap bullet | M1 demo realisation |
| -------------- | ------------------- |
| "two modules exchange message" | `m1-sender` → `m1-receiver` round-trip via `ipc_send` / `ipc_recv` across two PCBs with distinct `address_space_t` windows. Marker: `TEST:PASS:m1_ipc_allow`. |
| "unauthorized operation denied with explicit error" | `m1-unauth` `ipc_send` returns `IPC_ERR_CAP_DENIED`, emits `CAP:DENY:m1-unauth:CAP_IPC_SEND`, leaves `m1-receiver` blocked. Marker: `TEST:PASS:m1_ipc_deny`. |

Both markers are wired into `build/scripts/test.sh` and surface in the
validator JSON report (#110).

## Follow-up implementation issues to file

These execute issues are the concrete unit of work this plan unblocks. Each is
intended to be at most one PR. Proposed titles:

1. **"M1 proc: PCB table skeleton + `proc_init`/`proc_spawn_module` (BUILD_ROADMAP §5.1)"**
   - Lands `kernel/proc/process.{c,h}`, the static PCB array, the module
     registry stub, and `proc_init` wiring into `kernel_main`. No scheduler,
     no IPC, no aspace. Validator target: `proc_table_skeleton` asserts
     `proc_lookup(1)` returns the registered name.
2. **"M1 proc: flat-with-bounds `address_space_t` + `.proc_arena` linker carve-out (BUILD_ROADMAP §5.1)"**
   - Adds `kernel/proc/address_space.{c,h}`, `.proc_arena` section in
     `kernel/arch/x86/boot/linker.ld`, partitioning at boot. Validator
     target: `aspace_carve` asserts non-overlap of any two PCB windows.
3. **"M1 proc: cooperative scheduler + context switch on IPC block (BUILD_ROADMAP §5.1)"**
   - Adds the ready-queue, `proc_yield`, and the per-process kernel-stack
     swap. Depends on #180's port table existing so blocking has a
     destination. Validator target: `proc_sched_round_robin` runs two PCBs
     to completion in deterministic order.
4. **"M1 proc: two-module IPC demo + allow/deny acceptance harness (BUILD_ROADMAP §5.1)"**
   - Registers `m1-sender` / `m1-receiver` / `m1-unauth`, wires the
     `TEST:PASS:m1_ipc_allow` and `TEST:PASS:m1_ipc_deny` markers into
     `build/scripts/test.sh` and `validate_bundle.sh`. Depends on (1)–(3),
     #180, and #193.
5. **"M1 proc: syscall entry stub + reserved vector under OS_ABI_VERSION (BUILD_ROADMAP §5.1 / §7)"**
   - Lands `kernel/proc/syscall_entry.{c,h}` with the placeholder returning
     `IPC_ERR_INVALID_MSG`. Pure ABI reservation; no user-space caller yet.
     Depends on #150 landing the `OS_ABI_VERSION` anchor.

A separate, non-blocking housekeeping issue covers the eventual rename of
`kernel/user/process.c` → `kernel/user/launcher_exec.c` once the new
`kernel/proc/` home stops surprising readers.

## Out of scope (explicit non-asks)

- ELF loader changes (the existing `process.c` ELF path is untouched).
- Filesystem integration (M3).
- Console-service integration (M2).
- Capability broker (M4).
- SMP, preemption, demand paging, real ring transitions (M2+).
