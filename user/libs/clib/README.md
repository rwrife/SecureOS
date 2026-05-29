# user/libs/clib — freestanding userland libc nucleus

> **Owner:** in-OS toolchain (M7) / SDK runtime
> **Status:** allocator ([#404](https://github.com/rwrife/SecureOS/issues/404)) + string/memory family ([#407](https://github.com/rwrife/SecureOS/issues/407) slice 1)
> **Plan:** [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../../plans/2026-05-28-in-os-toolchain-self-hosting.md) (P1 + P3)

## What this is

The freestanding libc nucleus that the in-OS toolchain
([#403](https://github.com/rwrife/SecureOS/issues/403)) is being built
against. Today it ships:

- the heap allocator — `clib_malloc` / `free` / `realloc` / `calloc`
  (issue [#404](https://github.com/rwrife/SecureOS/issues/404)), and
- the freestanding `string.h` family — `mem*` (`memcpy`, `memmove`,
  `memset`, `memcmp`, `memchr`) and `str*` (`strlen`, `strnlen`,
  `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strncat`,
  `strchr`, `strrchr`, `strstr`) under `include/clib/string.h` — slice 1
  of M7-TOOLCHAIN-004 ([#407](https://github.com/rwrife/SecureOS/issues/407)).

Later slices of #407 add stdio (`fopen` / `fread` / `fwrite` /
`fclose` / `fprintf`) on top of `os_fs_*` + `os_console_write`,
`setjmp` / `longjmp`, and `qsort`. Symbol-set drift is pinned today
by `tests/clib_string_test.c` (`symbol_set_pinned`) and will be
extended as each slice lands.

## What it is **not**

- Not a hosted libc. No threads, no signals, no locale.
- Not on-image yet. The library is `host-testable today`; the on-image
  build (cross-compile into `artifacts/sdk/libclib.a`, link from
  `crt0`) is wired alongside the brk syscall in the
  M7-TOOLCHAIN-001 kernel-side follow-up — see "ABI status" below.

## Why a brk callback (not a fixed buffer)

The issue body permits two implementation paths: (a) the first
resident of `user/libs/clib`, or (b) "land it standalone and fold in
later". We picked (b) and parameterise arena growth on a
`clib_brk_fn` callback:

```c
typedef void *(*clib_brk_fn)(void *ctx, size_t delta);
int clib_malloc_init(void *seed_base,
                     size_t seed_size,
                     clib_brk_fn brk_fn,
                     void *brk_ctx);
```

- Host test (`tests/clib_malloc_test.c`) registers a brk shim backed by
  a single `malloc(3)` page-aligned buffer — no syscall dependency.
- On-target runtime (next slice) registers a one-line forward to
  `os_mem_brk(delta)` once that syscall lands. No other allocator
  code changes.

This keeps the allocator itself reviewable in isolation — a single
host unit test covers alloc/free/realloc, fragmentation/coalescing,
and the second-run isolation case from the M7 plan.

## ABI status

This slice is **userland-only** and does **not** bump
`OS_ABI_VERSION`. The `os_mem_brk` / `os_mem_map` syscalls + their
ABI surface declaration in `user/include/secureos_api.h` and the
matching kernel-side wiring (which DOES require an `OS_ABI_VERSION`
minor bump per the issue body) are the explicit kernel-side
follow-up. Filing as `M7-TOOLCHAIN-001-KERNEL` once this lands.

## Tests

```
$ bash build/scripts/test.sh clib_malloc
TEST:PASS:clib_malloc:basic_roundtrip
TEST:PASS:clib_malloc:realloc_growth_and_shrink
TEST:PASS:clib_malloc:calloc_zeroes
TEST:PASS:clib_malloc:coalesce_neighbours
TEST:PASS:clib_malloc:brk_growth
TEST:PASS:clib_malloc:out_of_arena_no_panic
TEST:PASS:clib_malloc:toolchain_heap_isolation
TEST:PASS:clib_malloc:min_seed_bytes_anchored
TEST:PASS:clib_malloc
```

The `toolchain_heap_isolation` marker is the same one called out in the
M7 plan acceptance for this slice.

```
$ bash build/scripts/test.sh clib_string
TEST:PASS:clib_string:memcpy_basic
TEST:PASS:clib_string:memmove_overlap_forward
TEST:PASS:clib_string:memmove_overlap_backward
TEST:PASS:clib_string:memset_fill
TEST:PASS:clib_string:memcmp_order
TEST:PASS:clib_string:memchr_hit_and_miss
TEST:PASS:clib_string:strlen_and_strnlen
TEST:PASS:clib_string:strcmp_order
TEST:PASS:clib_string:strncmp_bounded
TEST:PASS:clib_string:strcpy_and_strncpy_pad
TEST:PASS:clib_string:strcat_and_strncat
TEST:PASS:clib_string:strchr_and_strrchr
TEST:PASS:clib_string:strstr_hit_and_miss
TEST:PASS:clib_string:symbol_set_pinned
TEST:PASS:clib_string
```

The `symbol_set_pinned` marker is the drift guard called out in the
M7-TOOLCHAIN-004 acceptance — every shipped symbol must remain
reachable through a function pointer, so a TinyCC drop or an unrelated
PR cannot silently remove a family member.
