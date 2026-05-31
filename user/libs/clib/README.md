# user/libs/clib — freestanding userland libc nucleus

> **Owner:** in-OS toolchain (M7) / SDK runtime
> **Status:** slice 1 (allocator, issue [#404](https://github.com/rwrife/SecureOS/issues/404)), the `str*`/`mem*` slice of [#407](https://github.com/rwrife/SecureOS/issues/407), the ctype slice of [#407](https://github.com/rwrife/SecureOS/issues/407), the `<limits.h>` slice of [#407](https://github.com/rwrife/SecureOS/issues/407), and the `setjmp`/`longjmp` slice ([#446](https://github.com/rwrife/SecureOS/issues/446)) have landed; `stdio` ([#447](https://github.com/rwrife/SecureOS/issues/447)) follows.
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
  `strtok_r`) TinyCC's argv + include-path parsers link against, and
- the freestanding `<limits.h>` nucleus — `CHAR_BIT`, `SCHAR_MIN`/
  `SCHAR_MAX`/`UCHAR_MAX`, `SHRT_MIN`/`SHRT_MAX`/`USHRT_MAX`, `INT_MIN`/
  `INT_MAX`/`UINT_MAX`, `LONG_MIN`/`LONG_MAX`/`ULONG_MAX`, `LLONG_MIN`/
  `LLONG_MAX`/`ULLONG_MAX`, `CHAR_MIN`/`CHAR_MAX` under
  `include/clib/limits.h` — slice 8 of M7-TOOLCHAIN-004
  ([#407](https://github.com/rwrife/SecureOS/issues/407)); required for
  freestanding by C11 §4¶6, pinned at the x86_64 SysV values TinyCC
  (#408) targets, drift-anchored through a helper TU in `src/limits.c`, and
- the freestanding `<stdbool.h>` nucleus — `bool` / `true` / `false` /
  `__bool_true_false_are_defined` under `include/clib/stdbool.h` (slice
  9 of [#407](https://github.com/rwrife/SecureOS/issues/407)). Required
  by C11 §4¶6 for any freestanding implementation; TinyCC and pending
  #407 sibling slices link against it, and
- the freestanding `<stddef.h>` nucleus — `NULL`, `size_t`, `ptrdiff_t`,
  `wchar_t`, `max_align_t`, and `offsetof` under
  `include/clib/stddef.h` — slice 9 of M7-TOOLCHAIN-004
  ([#407](https://github.com/rwrife/SecureOS/issues/407)); required for
  freestanding by C11 §4¶6, bound to the compiler intrinsics
  `__SIZE_TYPE__` / `__PTRDIFF_TYPE__` / `__WCHAR_TYPE__` that TinyCC
  (#408) and the host toolchain both expose, drift-anchored through a
  helper TU in `src/stddef.c`.

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

No `OS_ABI_VERSION` bump (userland-only, additive). The slice-4
`strtol` / `strtoul` / `strtoll` / `strtoull` clamp paths now set
`errno = ERANGE` on overflow and `errno = EINVAL` on a bad `base`
argument (the canonical ISO C / POSIX contract TinyCC's driver
relies on to distinguish a clean clamp from a real overflow). The
clamp return values and `*endptr` semantics are unchanged.

## Slice 7 — bsearch (issue #407)

TinyCC's symbol-table lookup paths sort-then-search the same arrays
they feed to `qsort`; the C standard pairs the two in `<stdlib.h>`
precisely because callers typically need both. This slice ships the
companion `bsearch` so the TinyCC port (#408) can link the pair as
a unit.

Peer of the qsort slice (PR #418) — different header, different
source file, different `symbol_set_pinned` sub-marker; can land in
either order. No allocator dependency, no syscall dependency,
userland-only (no `OS_ABI_VERSION` bump).

**Shipped surface (1 symbol, canonical libc name):**

- `bsearch(key, base, nmemb, size, compar)` — iterative,
  overflow-safe midpoint (`lo + (hi - lo) / 2`), alignment-agnostic
  byte-wise pointer arithmetic so unaligned element widths (e.g.
  TinyCC's 3-byte symbol records) work correctly.

**Defensive contract** (same posture as the qsort slice's NULL /
empty handling): every input the canonical contract leaves UB
(NULL key, NULL base with nmemb>0, NULL compar, size==0) degrades
to `NULL` rather than dereferencing.

```
$ bash build/scripts/test.sh clib_bsearch
TEST:PASS:clib_bsearch:empty_returns_null
TEST:PASS:clib_bsearch:single_hit
TEST:PASS:clib_bsearch:single_miss
TEST:PASS:clib_bsearch:hit_at_first
TEST:PASS:clib_bsearch:hit_at_last
TEST:PASS:clib_bsearch:hit_in_middle
TEST:PASS:clib_bsearch:miss_below_range
TEST:PASS:clib_bsearch:miss_above_range
TEST:PASS:clib_bsearch:miss_between_neighbours
TEST:PASS:clib_bsearch:duplicates_returns_some_match
TEST:PASS:clib_bsearch:struct_elements_payload_intact
TEST:PASS:clib_bsearch:odd_size_unaligned_elements
TEST:PASS:clib_bsearch:large_array_no_overflow
TEST:PASS:clib_bsearch:defensive_null_key
TEST:PASS:clib_bsearch:defensive_null_compar
TEST:PASS:clib_bsearch:defensive_zero_size
TEST:PASS:clib_bsearch:defensive_null_base_nonzero_nmemb
TEST:PASS:clib_bsearch:symbol_set_pinned
TEST:PASS:clib_bsearch
```

`large_array_no_overflow` sweeps every even key (hit) and every odd
key (miss) across a 2048-element sorted array — same N as the
qsort slice's `large_pathological_no_overflow` — and also probes
out-of-range values below and above the populated span to exercise
the overflow-safe midpoint path.

## Slice — `<stdnoreturn.h>` nucleus (issue #407)

C11 §4¶6 lists `<stdnoreturn.h>` among the freestanding-required
headers; §7.23 defines the header as a single convenience macro
`noreturn` aliasing the C11 keyword `_Noreturn`. TinyCC (#408) and
any third-party SDK code consumed by the in-OS toolchain (#403) are
entitled to `#include <stdnoreturn.h>` and to spell abort-like
helpers with `noreturn` rather than the bare keyword. Landing the
macro now lets those consumers compile unchanged.

**Shipped surface:**

- `noreturn` — alias for `_Noreturn` (C11 §7.23¶1). Suppressed under
  `__cplusplus` because `_Noreturn` is not a C++ keyword and `noreturn`
  is a standard C++ attribute.
- Helper TU `src/stdnoreturn.c` — drift anchor that exports
  `clib_stdnoreturn_eval`, `clib_stdnoreturn_op_count`, and a real
  `noreturn`-decorated function `clib_stdnoreturn_loop_forever`,
  pinned by the `symbol_set_pinned` sub-marker.

```
$ bash build/scripts/test.sh clib_stdnoreturn
TEST:PASS:clib_stdnoreturn:macros_defined
TEST:PASS:clib_stdnoreturn:macros_expand_correctly
TEST:PASS:clib_stdnoreturn:helper_tu_agrees
TEST:PASS:clib_stdnoreturn:symbol_set_pinned
TEST:PASS:clib_stdnoreturn
```

No `OS_ABI_VERSION` bump (userland-only, additive, header-only).

## Slice 8 — `<stdio.h>` nucleus (issue [#447](https://github.com/rwrife/SecureOS/issues/447))

Final large gating slice before TinyCC can link (#408) and before the
`cc` driver (#409) can persist a `.sof` to disk. Ships the freestanding
`<stdio.h>` surface TinyCC and `cc` actually exercise — deliberately
minimal, no floats, no `scanf`, no locale.

**Backend abstraction.** `user/libs/clib/` MUST NOT include
`<secureos_api.h>` (the same containment rule the str/mem, ctype,
qsort, stdlib, errno, stdarg, bsearch slices follow). All I/O routes
through a `clib_stdio_backend_t` function-pointer table the embedder
registers via `clib_stdio_init`:

- on-target: the SDK app init layer fills the table with thunks to
  `os_fs_read_file` / `os_fs_write_file` / `os_console_write` (the
  embedder side is a small follow-up alongside the toolchain app
  itself; out of scope for the slice).
- host tests: `tests/clib_stdio_test.c` registers a recorder backend
  that captures bytes into an in-memory file table + console buffer,
  pins exact output, and asserts `fprintf(stderr, ...)` routes to the
  console sink (not the file sink) by handing the slice a backend
  with `write_file == NULL`.

**Shipped surface:**

- `FILE`, `stdin` / `stdout` / `stderr`
- `fopen` / `fclose` / `fread` / `fwrite` / `fflush`
- `fputs` / `fputc`
- `fprintf` / `vfprintf` / `printf`
- `feof` / `ferror`
- format spec set: `%s %d %i %u %x %p %c %% %ld %li %lu %lx`,
  optional width (`%8d`), zero-pad (`%08d`), left-justify (`%-5s`)

**Format walker quirks** (deliberate and pinned by the tests):

- unsupported specifiers (e.g. `%f`) echo the literal `%...` sequence
  rather than silently dropping it, so a regression is visible in the
  recorder buffer.
- `%p` always emits the `0x` prefix.
- vfprintf truncates at a 1 KiB stack-resident formatting buffer but
  still returns the full requested length so callers can detect it.

```
$ bash build/scripts/test.sh clib_stdio
TEST:PASS:clib_stdio:printf_basic_format
TEST:PASS:clib_stdio:printf_full_spec_set
TEST:PASS:clib_stdio:fopen_fwrite_fread_round_trip
TEST:PASS:clib_stdio:large_payload_round_trip
TEST:PASS:clib_stdio:stderr_routes_to_console
TEST:PASS:clib_stdio:fopen_invalid_mode_returns_null
TEST:PASS:clib_stdio:defensive_no_backend
TEST:PASS:clib_stdio:shutdown_resets_pool
TEST:PASS:clib_stdio:symbol_set_pinned
TEST:PASS:clib_stdio
```

`large_payload_round_trip` writes 4097 bytes (deliberately above the
4 KiB threshold called out in #447's acceptance — the same threshold
that exercises the multi-cluster FS write path landed in PR #411) and
asserts byte-exact round-trip through the recorder.

No `OS_ABI_VERSION` bump (userland-only, additive). The on-target
embedder side — a one-line `clib_stdio_init(&os_backend)` from the
toolchain app's `main` — lands with the TinyCC port (#408) /
`cc` driver (#409).

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

## Slice 10 — `<stdint.h>` nucleus (issue #407)

C11 §4¶6 / §7.20 mandate `<stdint.h>` on a freestanding
implementation. TinyCC ([#408](https://github.com/rwrife/SecureOS/issues/408))
and any non-trivial in-OS C source consume the exact-width / pointer-
width / max-width integer typedefs + their limit constants + the
`INTn_C` / `UINTn_C` constant-suffix macros. Header is wholly
freestanding (no syscall dependency); typedef widths and limits are
derived from the compiler-provided `__INT*_TYPE__` / `__INT*_MAX__`
builtins so the same source is target-correct on the x86_64 cross-
compiler and on the host gcc/clang the unit test runs under.

**Shipped symbols:**

- Exact-width typedefs (8): `int{8,16,32,64}_t`, `uint{8,16,32,64}_t`
- Pointer-width typedefs (2): `intptr_t`, `uintptr_t`
- Max-width typedefs (2): `intmax_t`, `uintmax_t`
- Limit macros: `INT{8,16,32,64}_{MIN,MAX}`, `UINT{8,16,32,64}_MAX`,
  `INTPTR_{MIN,MAX}`, `UINTPTR_MAX`, `INTMAX_{MIN,MAX}`, `UINTMAX_MAX`,
  `SIZE_MAX`, `PTRDIFF_{MIN,MAX}`
- Constant macros: `INT{8,16,32,64}_C(v)`, `UINT{8,16,32,64}_C(v)`,
  `INTMAX_C(v)`, `UINTMAX_C(v)`

Out of scope this slice: the `int_least*_t` / `int_fast*_t` families
(no TinyCC consumer pins them yet) and `<inttypes.h>` (sits on top of
`<stdio.h>`, which is its own deferred slice).

```
$ bash build/scripts/test.sh clib_stdint
TEST:PASS:clib_stdint:exact_widths_pinned
TEST:PASS:clib_stdint:pointer_widths_pinned
TEST:PASS:clib_stdint:max_widths_pinned
TEST:PASS:clib_stdint:limits_pinned
TEST:PASS:clib_stdint:size_and_ptrdiff_pinned
TEST:PASS:clib_stdint:const_macros_pinned
TEST:PASS:clib_stdint:symbol_set_pinned
TEST:PASS:clib_stdint
```

## Slice 11 — `<inttypes.h>` format-string nucleus (issue #407)

C11 §7.8 specifies `<inttypes.h>` as a layer on top of `<stdint.h>`
for printf/scanf format-string macros (`PRId32`, `SCNu64`, etc.).
TinyCC ([#408](https://github.com/rwrife/SecureOS/issues/408)) and
any non-trivial C source it compiles parse these macros pervasively.
This slice ships the macros only; the matching function family
(`imaxabs` / `imaxdiv` / `strtoimax` / `strtoumax`) is intentionally
deferred to a follow-on slice that lives next to its `<stdlib.h>`
peers (`abs` / `div` / `strtol`).

The macros are pure preprocessor — no `<stdio.h>` dependency at the
language level — so this slice does not block on the stdio nucleus
(#447 / PR #453). Length-modifier resolution is driven by the
compiler-provided `__INTn_FMTd__` builtins where available (Clang),
with a `__SIZEOF_LONG__` / `__SIZEOF_POINTER__` fallback that keeps
the header target-correct on both ILP32 and LP64 GCC.

**Shipped symbols:**

- Exact-width PRI / SCN: `PRI{d,i,u,o,x,X}{8,16,32,64}`,
  `SCN{d,i,u,o,x}{8,16,32,64}`
- Least-width / fast-width family rows (parity with slice 10b
  typedefs, PR #457): `PRI*LEAST{8,16,32,64}`, `PRI*FAST{8,16,32,64}`,
  and the matching `SCN*LEAST*` / `SCN*FAST*`
- Max-width: `PRI{d,i,u,o,x,X}MAX`, `SCN{d,i,u,o,x}MAX`
- Pointer-width: `PRI{d,i,u,o,x,X}PTR`, `SCN{d,i,u,o,x}PTR`

Out of scope this slice (deferred to a follow-on PR): `imaxabs`,
`imaxdiv`, `imaxdiv_t`, `strtoimax`, `strtoumax` — these belong with
their `<stdlib.h>` peers and want to share the overflow-clamp
plumbing the strtol{,l} slice landed.

```
$ bash build/scripts/test.sh clib_inttypes
TEST:PASS:clib_inttypes:macro_shape_pinned
TEST:PASS:clib_inttypes:printf_roundtrip_pinned
TEST:PASS:clib_inttypes:least_fast_format_pinned
TEST:PASS:clib_inttypes:max_ptr_roundtrip_pinned
TEST:PASS:clib_inttypes:symbol_set_pinned
TEST:PASS:clib_inttypes
```
