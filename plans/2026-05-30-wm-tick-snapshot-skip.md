# 2026-05-30 — WM tick snapshot skip + fast block copy

Follow-up to `2026-05-29-wm-render-speedup.md` (PR #429). After the per-pixel
VGA blit and per-pixel compositor loops were converted to bulk row copies, the
WM became fast enough to expose the *next* dominant cost: the per-tick ELF
region save/restore in `kernel/core/session_manager.c`.

## Problem

Every call to `session_manager_tick(sid)` from the WM (once per active window
per frame) did **three 128 KB byte-by-byte memcpy loops** to save the parent
ELF, save the child ELF, and restore the parent ELF — 384 KB of byte-wise
copy per WM frame per window, at `-O0`. With the faster compositor the WM
loop ran many more times per second, multiplying this overhead. User reported
the WM was "smooth for a second, then slow for a second" — consistent with
the CPU saturating on these copies under varying load.

The overhead also fired even when the session had **no work to do** (no
injected input, not blocked) — the trampoline would call
`console_process_injected()` which is a no-op when the inject buffer is
empty, but the surrounding ELF dance still ran.

## Fix

Two layered changes in `kernel/core/session_manager.c`:

1. **Skip the entire dance when the session is idle.** If
   `!blocked && inject_head == inject_tail`, the tick would do no useful
   work; return immediately. For a freshly-opened WM child window with the
   kernel built-in console (the common case before the user runs any
   long-running interactive binary like `sosh`), this eliminates all three
   128 KB copies per frame.

2. **Replace byte loops with `rep movsq`.** When the dance is needed (input
   pending or session blocked mid-command), use an inline-asm `cld; rep
   movsq` over 8-byte aligned buffers to copy 8 bytes per microcode
   iteration at memory-bandwidth speed instead of the byte-wise loop.
   `g_elf_parent_buf` is explicitly `__attribute__((aligned(16)))`;
   `ELF_REGION_START = 0x800000` is page-aligned; `kmalloc` returns aligned
   pointers; `ELF_SNAPSHOT_SIZE = 128*1024` is divisible by 8.

## Files changed

- `kernel/core/session_manager.c`
  - Add `fast_block_copy()` static inline using `rep movsq`.
  - Add `__attribute__((aligned(16)))` on `g_elf_parent_buf`.
  - In `session_manager_tick`, add the idle-skip fast-path.
  - Replace all three byte loops with `fast_block_copy()` calls.

## Verification

- `scripts/build.ps1 force` succeeds, all PASS lines green.
- Booting in QEMU, user opens WM and verifies mouse / drag is smooth without
  periodic stutter.
