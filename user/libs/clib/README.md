# user/libs/clib — freestanding userland libc nucleus

> **Owner:** in-OS toolchain (M7) / SDK runtime
> **Status:** slice 1 (allocator, issue [#404](https://github.com/rwrife/SecureOS/issues/404)), the `str*`/`mem*` slice of [#407](https://github.com/rwrife/SecureOS/issues/407), the ctype / qsort / stdlib / errno / stdarg slices of [#407](https://github.com/rwrife/SecureOS/issues/407), and the `setjmp`/`longjmp` slice ([#446](https://github.com/rwrife/SecureOS/issues/446)) have landed; `stdio` ([#447](https://github.com/rwrife/SecureOS/issues/447)) follows.
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
  Slice 12 of #407 extends the same header with the freestanding
  tokenize / span family (`strspn`, `strcspn`, `strpbrk`, `strtok`,
  `strtok_r`) TinyCC's argv + include-path parsers link against.
  of M7-TOOLCHAIN-004 ([#407](https://github.com/rwrife/SecureOS/issues/407)).

Later slices of #407 add stdio (`fopen` / `fread` / `fwrite` /
`fclose` / `fprintf`) on top of `os_fs_*` + `os_console_write`. The
freestanding `setjmp`/`longjmp` pair landed via
[#446](https://github.com/rwrife/SecureOS/issues/446) as hand-rolled
i386 + x86_64 SysV callee-saved snapshot/restore in
`src/setjmp_x86.S`. Symbol-set drift is pinned per slice via each
test's `symbol_set_pinned` sub-marker (see `tests/clib_*_test.c`) and
will be extended as each slice lands.

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

$ bash build/scripts/test.sh clib_qsort
TEST:PASS:clib_qsort:empty_no_op
TEST:PASS:clib_qsort:single_no_op
TEST:PASS:clib_qsort:sorted_idempotent
TEST:PASS:clib_qsort:reverse_sorted
TEST:PASS:clib_qsort:random_ints
TEST:PASS:clib_qsort:duplicates_grouped
TEST:PASS:clib_qsort:small_under_insertion_threshold
TEST:PASS:clib_qsort:large_pathological_no_overflow
TEST:PASS:clib_qsort:struct_elements
TEST:PASS:clib_qsort:byte_elements_size_one
TEST:PASS:clib_qsort:odd_size_unaligned_elements
TEST:PASS:clib_qsort:stable_against_model
TEST:PASS:clib_qsort:symbol_set_pinned
TEST:PASS:clib_qsort
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

## Slice 5 — `<errno.h>` nucleus (issue #407)

PR #428's freestanding `stdlib.c` documents in its header that the
in-OS toolchain libc has "no errno" — `strtol` / `strtoul` overflow
paths clamp silently because the canonical POSIX `errno = ERANGE`
assignment had nowhere to land. TinyCC's driver (#408) reads `errno`
after `strtol` to distinguish a clean clamp from a real overflow, so
this slice lands the symbol + macro family ahead of that consumer.

**Shipped surface:**

- Storage: `int errno;` (writable global, no `__errno_location`
  indirection — SecureOS userland is single-threaded at
  `OS_ABI_VERSION = 0`).
- Macros (musl / Linux numbering, drift-pinned by
  `macro_values_pinned`): `EPERM`, `ENOENT`, `EIO`, `EBADF`, `ENOMEM`,
  `EACCES`, `EFAULT`, `EBUSY`, `EEXIST`, `ENOTDIR`, `EISDIR`,
  `EINVAL`, `ENFILE`, `EMFILE`, `ENOSPC`, `ESPIPE`, `EROFS`, `ERANGE`,
  `ENOSYS`, `ENOTSUP`, `EOVERFLOW`.
- Helper: `const char *clib_strerror(int)` — bounded ASCII, never
  `NULL`, returns `"Unknown error"` for unrecognised codes.

```
$ bash build/scripts/test.sh clib_errno
TEST:PASS:clib_errno:macro_values_pinned
TEST:PASS:clib_errno:errno_global_zero_init
TEST:PASS:clib_errno:errno_writable_roundtrip
TEST:PASS:clib_errno:errno_address_stable
TEST:PASS:clib_errno:strerror_known_codes
TEST:PASS:clib_errno:strerror_unknown_code
TEST:PASS:clib_errno:symbol_set_pinned
TEST:PASS:clib_errno
```

No `OS_ABI_VERSION` bump (userland-only, additive). A follow-up
slice can flip the existing `strtol`/`strtoul` clamp paths from
"silent" to `errno = ERANGE` without touching this slice's symbol
surface.

## Slice 1 — string/memory family (issue #407)

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
TEST:PASS:clib_string:strspn_basic
TEST:PASS:clib_string:strcspn_basic
TEST:PASS:clib_string:strpbrk_hit_and_miss
TEST:PASS:clib_string:strtok_walks_tokens
TEST:PASS:clib_string:strtok_r_reentrant_independence
TEST:PASS:clib_string:symbol_set_pinned
TEST:PASS:clib_string
```

The `symbol_set_pinned` marker is the drift guard called out in the
M7-TOOLCHAIN-004 acceptance — every shipped symbol must remain
reachable through a function pointer, so a TinyCC drop or an unrelated
PR cannot silently remove a family member.
