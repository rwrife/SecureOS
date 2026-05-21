# 0001. Boot protocol: Multiboot v1 + 64-bit long mode

- Status: Accepted
- Date: 2026-05-14

## Context

`BUILD_ROADMAP.md` §3.2 ("Bootloader strategy options") presented two paths:

- **Option A (fastest path):** Limine / Multiboot2-compliant boot protocol —
  less handwritten boot code, faster early momentum, slightly less ownership
  of the very earliest stage.
- **Option B (educational / full control):** custom stage-1/stage-2
  bootloader with hand-rolled real-mode → protected-mode → long-mode
  transitions.

The roadmap explicitly recommended **Option A first**, with an optional later
custom loader milestone.

In practice, the kernel boot path has materialized as **Multiboot v1 +
in-kernel 32-bit → 64-bit long-mode transition**, loaded by GRUB:

- `86c3768 feat(boot): transition kernel to 64-bit long mode via Multiboot entry`
- `0f419e3 fix for networking and filedemo, migrate to 64 bit`
- `13a817a network and boot fixes`

The contract is documented at the top of
[`kernel/arch/x86/boot/entry.asm`](../../../kernel/arch/x86/boot/entry.asm)
and in [`docs/BOOT_ENTRY_X86.md`](../../BOOT_ENTRY_X86.md), but no ADR
recorded the decision itself or what downstream code may rely on. This ADR
backfills that record so future agents touching boot, paging, or early
console do not need to reconstruct it from commit history.

## Decision

SecureOS x86 boots via **Multiboot v1** (per the spec recognised by GRUB
Legacy and GRUB 2), and the kernel image itself performs the transition
from the 32-bit protected mode handed in by the loader into **64-bit long
mode** before calling `kmain`.

Concretely:

1. The kernel image embeds a Multiboot v1 header (`MAGIC = 0x1BADB002`,
   `FLAGS = ALIGN | MEMINFO`) in its first 8 KiB. GRUB hands control to
   `_start` in 32-bit protected mode with `EAX = 0x2BADB002`,
   `EBX = phys addr of multiboot info`, interrupts disabled, paging
   disabled, and GRUB-supplied flat 32-bit segments.
2. `_start` installs a minimal 64-bit GDT, builds an identity-mapped page
   table covering the first 4 MiB with 2 MiB huge pages, enables PAE
   (`CR4.PAE`), sets `EFER.LME` via MSR `0xC0000080`, and enables paging
   (`CR0.PG`) — at which point the CPU enters long mode.
3. A far jump into the 64-bit code segment lands in `_start64`, which
   reloads data segments, sets a 64-bit stack, and calls `kmain`.
4. `kmain` runs in 64-bit long mode against an identity-mapped low-physical
   memory range, with COM1 serial and VGA text already usable as early
   sinks.

Option B (custom stage-1/stage-2 loader) is **not** pursued at this time and
is deferred indefinitely. A future ADR may introduce a custom loader as an
additive milestone if there is a concrete reason; until then, GRUB +
Multiboot v1 is the single supported entry path on x86.

Note on `v1` vs `v2`: §3.2 referenced Multiboot **2** as the fastest path.
We adopted Multiboot **v1** because GRUB Legacy/GRUB 2 both consume it
unchanged, the header is trivial (3 dwords), and nothing the kernel
currently needs requires v2 features (framebuffer tags, EFI memory map,
etc.). This is treated as a minor refinement of Option A rather than a
separate option; a later ADR can promote to v2 if/when the kernel needs
its richer info tags.

## Consequences

Downstream subsystems may now assume, by contract:

- **Entry mode:** `kmain` is entered in 64-bit long mode with paging
  enabled. Kernel C code is built for `x86_64`.
- **Paging:** an identity map of at least the first 4 MiB exists using
  2 MiB huge pages in PML4 → PDPT → PD. Any subsystem that wants a
  different layout (higher-half kernel, larger map, finer-grained pages)
  must build it on top, not assume it inherits a blank slate.
- **Registers at handoff:** the original Multiboot magic/info pointer
  semantics are the 32-bit entry's responsibility; by the time `kmain`
  runs, any info the kernel wants from the loader must have been
  preserved by the assembly entry path.
- **Loader assumptions:** the build produces a Multiboot-v1 image bootable
  by GRUB. The CI image and `build/scripts/run_qemu.sh` are organised
  around that. Switching loaders is a coordinated, ADR-level change.
- **Early I/O:** COM1 serial and VGA text-mode at `0xB8000` are the
  blessed early sinks for boot diagnostics (see
  `docs/BOOT_ENTRY_X86.md`).
- **What Option B would have changed:** with a custom loader, the kernel
  would not embed a Multiboot header, would receive control in real mode,
  and would own the A20, GDT bootstrap, and protected-mode transition
  itself. That code currently does not exist and is not on the roadmap.

## References

- `BUILD_ROADMAP.md` §3.2 (bootloader strategy options), §3.3 (boot
  implementation backlog), §3.5 (done definition for first goal)
- [`docs/BOOT_ENTRY_X86.md`](../../BOOT_ENTRY_X86.md)
- [`kernel/arch/x86/boot/entry.asm`](../../../kernel/arch/x86/boot/entry.asm)
- [`kernel/arch/x86/boot/linker.ld`](../../../kernel/arch/x86/boot/linker.ld)
- [`kernel/core/kmain.c`](../../../kernel/core/kmain.c)
- Commits: `86c3768`, `0f419e3`, `13a817a`
- Related (cross-link once landed): #93 (ABI reference), #109
  (test-plans / milestone registry)
