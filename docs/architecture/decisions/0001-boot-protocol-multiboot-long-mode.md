# 0001. Boot protocol: Multiboot v1 + 64-bit long mode

- Status: Accepted
- Date: 2026-05-13

## Context

`BUILD_ROADMAP.md` §3.2 ("Bootloader strategy options") laid out two
paths for the earliest boot stage:

- **Option A (fastest path)**: a Multiboot2/Limine-compliant boot
  protocol, leaning on an existing loader (GRUB / Limine) for the
  real-mode → protected-mode transition and ELF loading.
- **Option B (educational/full control)**: a custom stage-1/stage-2
  bootloader with hand-rolled real-mode → protected-mode → long-mode
  transitions.

The roadmap recommended **Option A first**, with an optional later
custom-loader milestone if educational value warranted it.

In practice, the kernel has converged on a concrete realization of
Option A:

- The kernel image embeds a **Multiboot v1** header in the first 8 KiB
  of the image (`kernel/arch/x86/boot/entry.asm`, `MULTIBOOT_MAGIC =
  0x1BADB002`, with `ALIGN | MEMINFO` flags).
- It is loaded by **GRUB** at the canonical 1 MiB physical base
  (`kernel/arch/x86/boot/linker.ld`, `. = 1M;`,
  `OUTPUT_FORMAT(elf64-x86-64)`).
- `_start` runs in 32-bit protected mode (per Multiboot contract),
  installs a minimal 64-bit GDT, builds identity-map page tables for the
  first 4 MiB using two 2 MiB huge pages, enables PAE, sets EFER.LME,
  enables paging, and far-jumps into a 64-bit code segment to reach
  `_start64`, which sets up the 64-bit stack and calls `kmain`.
- This is documented as current state in `docs/BOOT_ENTRY_X86.md`.

This convergence has happened over several commits without an ADR
recording the decision:

- `86c3768` — `feat(boot): transition kernel to 64-bit long mode via
  Multiboot entry`
- `0f419e3` — `fix for networking and filedemo, migrate to 64 bit`
- `13a817a` — `network and boot fixes`

Before this ADR, future agents touching boot, paging, early console, or
the QEMU harness had no single source explaining *why* the kernel
assumes Multiboot v1 + 64-bit long mode at entry, or what would change
under a different protocol.

## Decision

SecureOS commits to **Option A**, concretely realized as:

1. **Boot protocol: Multiboot v1** (`0x1BADB002`) with the
   `ALIGN | MEMINFO` flag set. The Multiboot header lives in the
   `.multiboot` section, placed within the first 8 KiB of the kernel
   image.
2. **Reference loader: GRUB** (Multiboot v1 compliant). Any
   Multiboot v1 compliant loader is acceptable; GRUB is the one wired
   into the build/QEMU harness today.
3. **Kernel entry contract**: GRUB hands control to `_start` in
   **32-bit protected mode**, with `EAX = 0x2BADB002` and `EBX`
   pointing at the Multiboot info structure, interrupts disabled,
   paging disabled, GRUB-supplied flat 32-bit segments active.
4. **Target CPU mode: 64-bit long mode.** The kernel transitions to
   long mode in its own boot stub (PAE + identity-mapped first 4 MiB
   via two 2 MiB huge pages + EFER.LME + CR0.PG + far-jump into 64-bit
   CS) before calling `kmain`. `kmain` and all higher-level kernel code
   runs in 64-bit mode at an identity-mapped virtual == physical
   address starting at 1 MiB.
5. **Image layout: ELF64**, loaded at physical/virtual 1 MiB. All
   sections must fit within the first 4 MiB so the bootstrap
   identity-map page tables fully cover the kernel.

Multiboot2 and Limine remain compatible with Option A in spirit, but
are explicitly **not adopted** at this time to avoid churn against the
working Multiboot v1 path.

A later ADR may revisit this decision (e.g. to add a Multiboot2 header
for richer memory maps, or to introduce a custom stage-1/stage-2 loader
for educational value as called out in roadmap §3.2 Option B). Any such
change must supersede this ADR rather than silently diverge.

## Consequences

What now depends on this decision:

- **Paging setup** (`kernel/arch/x86/boot/entry.asm`): bootstrap
  identity-map page tables are sized for the first 4 MiB only; growing
  beyond that requires either expanding the bootstrap page tables or
  switching to a higher-half mapping ADR.
- **Linker layout** (`kernel/arch/x86/boot/linker.ld`):
  `OUTPUT_FORMAT(elf64-x86-64)`, `ENTRY(_start)`, and `. = 1M;` are all
  Multiboot/long-mode assumptions; changing the protocol changes this
  file.
- **Early serial / VGA console** (`kernel/arch/x86/serial.c`,
  `kernel/arch/x86/vga.c`): assume long-mode addressing and that GRUB
  has already left the platform in a state where `0xB8000` and COM1
  ports are usable.
- **QEMU harness and debug-exit**: the headless harness boots the ELF
  via a Multiboot-compatible flow and reads serial markers; the
  contract above is what the harness expects.
- **Build pipeline** (`build/scripts/build_kernel_entry.sh` and
  friends): produces the ELF64 kernel image at 1 MiB; deterministic
  build invariants assume this layout.

What **Option B** would have changed instead:

- We would own real-mode entry, A20, the protected-mode GDT setup,
  ELF/raw image loading, and the long-mode transition end-to-end.
- The build pipeline would produce stage-1/stage-2 binaries plus the
  kernel image, and the QEMU harness would boot the loader rather than
  the ELF directly.
- Early debugging surface would be larger (more handwritten asm before
  any serial output is reliable).

Because Option A was chosen, we accept slightly less ownership of the
very earliest stage in exchange for a much smaller boot-stub surface
area and a working long-mode `kmain` today.

## References

- `BUILD_ROADMAP.md` §3.2 (Bootloader strategy options) and §3.3
  (Concrete implementation backlog).
- `BUILD_ROADMAP.md` §8 ("Immediate Next 14 Tasks") item 14 — the
  task that motivated this ADR.
- `docs/BOOT_ENTRY_X86.md` — current-state description of the boot
  entry, build artifacts, and the `kmain` handoff.
- Code paths:
  - `kernel/arch/x86/boot/entry.asm` — Multiboot header, 32-bit
    `_start`, long-mode transition, `_start64`.
  - `kernel/arch/x86/boot/linker.ld` — ELF64 layout, 1 MiB load base.
  - `kernel/core/kmain.c` — 64-bit `kmain` entrypoint.
  - `kernel/arch/x86/serial.c`, `kernel/arch/x86/vga.c` — early
    console drivers that assume the post-handoff environment defined
    above.
- Commits: `86c3768`, `0f419e3`, `13a817a`.
- Related issues: #93 (ABI reference), #109 (test-plans registry) —
  ADR index, ABI reference, and milestone registry together form the
  "ground truth" surface future agents read first.
