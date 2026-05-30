# `libclib.a` Public Symbol Surface

Status: PINNED at `OS_ABI_VERSION = 0`
Owner: M7-TOOLCHAIN-004 (umbrella #403, slice issue #407)
Last reviewed: 2026-05-30 (issue #449)

## Why this exists

`user/libs/clib/` builds into `libclib.a`, the freestanding libc nucleus the
in-OS toolchain (TinyCC port #408, `cc` driver #409) and any third-party
SecureOS app will link against once the in-OS build chain comes online. Per
#407's acceptance ("Symbol list documented and pinned by a drift test") and
BUILD_ROADMAP §7 (ABI / interface freeze), the public symbol surface must
be frozen explicitly rather than emerge from whatever `nm` happens to print
on `main`.

This file is the **human-readable** half of that freeze: one row per
exported symbol, grouped by defining header, with the originating slice /
PR. The **machine-checkable** half lives in
[`../../tests/data/clib_symbols.expected`](../../tests/data/clib_symbols.expected),
which is diffed against both `nm -g --defined-only` of a freshly built
host-side `libclib.a` AND against the canonical block at the bottom of
this file by `build/scripts/test_clib_symbol_drift.sh`. Adding or removing
a symbol fails the gate unless **all three** sources (doc table, canonical
pin block, library archive) are updated in the same change.

This mirrors the existing precedent of `capability_registry_drift`
(`docs/abi/capability-registry.json`) and `abi_stamps_drift`.

## Stability

While `OS_ABI_VERSION == 0` the clib surface is **additive**: new symbols
may be added (with an explicit doc + pin update in the same PR), but no
shipped symbol may be removed or change signature. Versioning of
`libclib.a` follows the umbrella ABI policy in
[`versioning.md`](versioning.md) — there is no separate libclib version
counter at v0.

## Surface

### `clib/ctype.h` (slice 2 / PR #417)

| Symbol      | Signature                | Notes                                  |
|-------------|--------------------------|----------------------------------------|
| `isascii`   | `int isascii(int c)`     | non-standard but pinned for TinyCC     |
| `isdigit`   | `int isdigit(int c)`     |                                        |
| `isxdigit`  | `int isxdigit(int c)`    |                                        |
| `isalpha`   | `int isalpha(int c)`     |                                        |
| `isalnum`   | `int isalnum(int c)`     |                                        |
| `isspace`   | `int isspace(int c)`     |                                        |
| `isblank`   | `int isblank(int c)`     |                                        |
| `isupper`   | `int isupper(int c)`     |                                        |
| `islower`   | `int islower(int c)`     |                                        |
| `iscntrl`   | `int iscntrl(int c)`     |                                        |
| `isprint`   | `int isprint(int c)`     |                                        |
| `isgraph`   | `int isgraph(int c)`     |                                        |
| `ispunct`   | `int ispunct(int c)`     |                                        |
| `toupper`   | `int toupper(int c)`     |                                        |
| `tolower`   | `int tolower(int c)`     |                                        |

### `clib/errno.h` (slice 5 / PR #430)

| Symbol          | Signature                              | Notes                                  |
|-----------------|----------------------------------------|----------------------------------------|
| `errno`         | `int errno;`                           | writable global; no per-thread storage |
| `clib_strerror` | `const char *clib_strerror(int e)`     | bounded ASCII, never NULL              |

### `clib/malloc.h` (slice 0 / pre-#407, issue #404)

| Symbol                        | Signature                                                          | Notes |
|-------------------------------|--------------------------------------------------------------------|-------|
| `clib_malloc_init`            | `int clib_malloc_init(void *seed_base, size_t seed_bytes, clib_brk_fn brk, void *brk_ctx)` | |
| `clib_malloc_shutdown`        | `void clib_malloc_shutdown(void)`                                  | |
| `clib_malloc_min_seed_bytes`  | `size_t clib_malloc_min_seed_bytes(void)`                          | |
| `clib_malloc`                 | `void *clib_malloc(size_t size)`                                   | |
| `clib_free`                   | `void clib_free(void *ptr)`                                        | |
| `clib_realloc`                | `void *clib_realloc(void *ptr, size_t size)`                       | |
| `clib_calloc`                 | `void *clib_calloc(size_t nmemb, size_t size)`                     | |
| `clib_malloc_get_stats`       | `void clib_malloc_get_stats(clib_malloc_stats_t *out)`             | |

### `clib/qsort.h` (slice 3 / PR #418)

| Symbol  | Signature                                                                                 |
|---------|-------------------------------------------------------------------------------------------|
| `qsort` | `void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))`   |

### `clib/stdarg.h` (slice 6 / PR #431)

| Symbol                          | Signature                                  | Notes |
|---------------------------------|--------------------------------------------|-------|
| `clib_stdarg_sum_ints`          | `int clib_stdarg_sum_ints(int n, ...)`     | drift anchor for the va_arg macros |
| `clib_stdarg_sum_ints_via_copy` | `int clib_stdarg_sum_ints_via_copy(int n, ...)` | drift anchor for `va_copy` |

### `clib/stdlib.h` (slice 4 / PR #428)

| Symbol    | Signature                                                          | Notes |
|-----------|--------------------------------------------------------------------|-------|
| `atoi`    | `int atoi(const char *s)`                                          | |
| `strtol`  | `long strtol(const char *s, char **end, int base)`                 | overflow clamps; `<errno.h>` wiring deferred |
| `strtoul` | `unsigned long strtoul(const char *s, char **end, int base)`       | overflow clamps |
| `abs`     | `int abs(int x)`                                                   | INT_MIN UB-safe (input == INT_MIN saturates to INT_MAX) |
| `labs`    | `long labs(long x)`                                                | LONG_MIN UB-safe |

### `clib/string.h` (slice 1 / PR #416)

| Symbol    | Signature                                                          |
|-----------|--------------------------------------------------------------------|
| `memcpy`  | `void *memcpy(void *dst, const void *src, size_t n)`               |
| `memmove` | `void *memmove(void *dst, const void *src, size_t n)`              |
| `memset`  | `void *memset(void *dst, int c, size_t n)`                         |
| `memcmp`  | `int memcmp(const void *a, const void *b, size_t n)`               |
| `memchr`  | `void *memchr(const void *s, int c, size_t n)`                     |
| `strlen`  | `size_t strlen(const char *s)`                                     |
| `strnlen` | `size_t strnlen(const char *s, size_t max)`                        |
| `strcmp`  | `int strcmp(const char *a, const char *b)`                         |
| `strncmp` | `int strncmp(const char *a, const char *b, size_t n)`              |
| `strcpy`  | `char *strcpy(char *dst, const char *src)`                         |
| `strncpy` | `char *strncpy(char *dst, const char *src, size_t n)`              |
| `strcat`  | `char *strcat(char *dst, const char *src)`                         |
| `strncat` | `char *strncat(char *dst, const char *src, size_t n)`              |
| `strchr`  | `char *strchr(const char *s, int c)`                               |
| `strrchr` | `char *strrchr(const char *s, int c)`                              |
| `strstr`  | `char *strstr(const char *hay, const char *needle)`                |

## Canonical pin

The block below is the **machine-checkable** symbol pin. It MUST stay
byte-identical (modulo trailing newline) to
[`../../tests/data/clib_symbols.expected`](../../tests/data/clib_symbols.expected),
and every symbol named in any table above MUST appear here. The
`clib_symbol_drift` test enforces both invariants.

<!-- clib-symbols:begin -->
```
abs
atoi
clib_calloc
clib_free
clib_malloc
clib_malloc_get_stats
clib_malloc_init
clib_malloc_min_seed_bytes
clib_malloc_shutdown
clib_realloc
clib_stdarg_sum_ints
clib_stdarg_sum_ints_via_copy
clib_strerror
errno
isalnum
isalpha
isascii
isblank
iscntrl
isdigit
isgraph
islower
isprint
ispunct
isspace
isupper
isxdigit
labs
memchr
memcmp
memcpy
memmove
memset
qsort
strcat
strchr
strcmp
strcpy
strlen
strncat
strncmp
strncpy
strnlen
strrchr
strstr
strtol
strtoul
tolower
toupper
```
<!-- clib-symbols:end -->

## How to update

When a new clib symbol is added (or an obsolete one removed) in the
**same PR** that lands the source change:

1. Add (or remove) the row in the appropriate header table above.
2. Add (or remove) the name in the `<!-- clib-symbols:begin -->`
   canonical pin block above. Keep the block **lexically sorted** —
   the drift script asserts sort order.
3. Add (or remove) the same line in
   `tests/data/clib_symbols.expected`. Same sort order.
4. Re-run `bash build/scripts/test.sh clib_symbol_drift` locally. All
   four sub-markers must pass.

Step 4 is what the bundle gate (`validate_bundle.sh` `TEST_TARGETS`)
runs in CI, so if you forget any of steps 1-3 the bundle flips to FAIL
with a descriptive marker pointing at which source disagreed.
