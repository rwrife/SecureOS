# user/libs/clib — freestanding userland libc nucleus

> **Owner:** in-OS toolchain (M7) / SDK runtime
> **Status:** slice 1 (allocator, issue [#404](https://github.com/rwrife/SecureOS/issues/404)) and slice 2 (ctype family, issue [#407](https://github.com/rwrife/SecureOS/issues/407)) landed; `str*`/`mem*` (also #407) in flight via PR #416; `stdio` / `setjmp` follow on later slices.
> **Plan:** [`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../../plans/2026-05-28-in-os-toolchain-self-hosting.md) (P1)

## What this is

The freestanding libc nucleus that the in-OS toolchain
([#403](https://github.com/rwrife/SecureOS/issues/403)) is being built
against. Today it ships **only the heap allocator** —
`clib_malloc`/`free`/`realloc`/`calloc` — because the immediate caller
(TinyCC, P4) requires dynamic allocation and the rest of the libc
surface (`str*` / `mem*` / `stdio` over `os_fs_*`) lands in
M7-TOOLCHAIN-004 ([#407](https://github.com/rwrife/SecureOS/issues/407)).

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

## Slice 2 — ctype family (issue #407)

TinyCC's preprocessor and tokenizer (`tccpp.c`) need the classic ctype
predicates. They have no syscall dependency, so this slice lands in
parallel with the `str*`/`mem*` slice (PR #416) — different header,
different source file, different `symbol_set_pinned` sub-marker.

**Shipped symbols (15):**

- Classification: `isascii`, `isdigit`, `isxdigit`, `isalpha`,
  `isalnum`, `isspace`, `isblank`, `isupper`, `islower`, `iscntrl`,
  `isprint`, `isgraph`, `ispunct`
- Conversion: `toupper`, `tolower`

Canonical libc names; ASCII only, no locale, no `_l` variants.
Compiled with `-fno-builtin` in the host test so the assertions
exercise our implementations rather than `__builtin_isdigit` etc.

```
$ bash build/scripts/test.sh clib_ctype
TEST:PASS:clib_ctype:isascii_full_range
TEST:PASS:clib_ctype:isdigit_full_range
TEST:PASS:clib_ctype:isxdigit_full_range
TEST:PASS:clib_ctype:isalpha_full_range
TEST:PASS:clib_ctype:isalnum_full_range
TEST:PASS:clib_ctype:isspace_full_range
TEST:PASS:clib_ctype:isblank_full_range
TEST:PASS:clib_ctype:isupper_full_range
TEST:PASS:clib_ctype:islower_full_range
TEST:PASS:clib_ctype:iscntrl_full_range
TEST:PASS:clib_ctype:isprint_full_range
TEST:PASS:clib_ctype:isgraph_full_range
TEST:PASS:clib_ctype:ispunct_full_range
TEST:PASS:clib_ctype:toupper_full_range
TEST:PASS:clib_ctype:tolower_full_range
TEST:PASS:clib_ctype:eof_safe
TEST:PASS:clib_ctype:symbol_set_pinned
TEST:PASS:clib_ctype
```

`symbol_set_pinned` is the drift marker required by the #407
acceptance: every shipped symbol must remain reachable through a
function pointer, so a TinyCC drop or unrelated PR cannot silently
remove a family member.
