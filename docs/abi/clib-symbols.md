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

### `clib/bsearch.h` (slice 7 / PR #433)

| Symbol     | Signature                                                                                  | Notes |
| ---------- | ------------------------------------------------------------------------------------------ | ----- |
| `bsearch`  | `void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *))` | freestanding peer of `qsort` |

### `clib/limits.h` (slice 8 / PR #434)

| Symbol                     | Signature                                | Notes |
| -------------------------- | ---------------------------------------- | ----- |
| `clib_limits_char_bit`     | `int  clib_limits_char_bit(void)`        | drift anchor for `CHAR_BIT` |
| `clib_limits_check_value`  | `long clib_limits_check_value(int which)` | drift anchor for `<limits.h>` macros |

### `clib/stdbool.h` (slice 9 / PR #435)

| Symbol                                | Signature                                       | Notes |
| ------------------------------------- | ----------------------------------------------- | ----- |
| `clib_stdbool_true_value`             | `int clib_stdbool_true_value(void)`             | drift anchor for `true` |
| `clib_stdbool_false_value`            | `int clib_stdbool_false_value(void)`            | drift anchor for `false` |
| `clib_stdbool_sizeof_bool`            | `int clib_stdbool_sizeof_bool(void)`            | drift anchor for `sizeof(bool)` |
| `clib_stdbool_feature_macro_value`    | `int clib_stdbool_feature_macro_value(void)`    | drift anchor for `__bool_true_false_are_defined` |

Note: `clib/stddef.h` (slice 9 / PR #436) ships drift-anchor helpers via
`src/stddef.c`; see its own section below.

### `clib/stddef.h` (slice 9 / PR #436)

| Symbol                          | Signature                                | Notes |
| ------------------------------- | ---------------------------------------- | ----- |
| `clib_stddef_sizeof`            | `unsigned long clib_stddef_sizeof(int which)` | drift anchor for `size_t` / `ptrdiff_t` / `wchar_t` / `max_align_t` widths |
| `clib_stddef_offsetof_probe`    | `unsigned long clib_stddef_offsetof_probe(int which)` | drift anchor for `offsetof` |

### `clib/stdint.h` (slice 10 / PR #457)

| Symbol                | Signature                                 | Notes |
| --------------------- | ----------------------------------------- | ----- |
| `clib_stdint_sizeof`  | `unsigned long clib_stdint_sizeof(int which)` | drift anchor for exact-width / pointer-width / max-width typedef sizes |
| `clib_stdint_maxof`   | `unsigned long long clib_stdint_maxof(int which)` | drift anchor for `INTn_MAX`/`UINTn_MAX`/`SIZE_MAX`/`PTRDIFF_MAX` |

### `clib/inttypes.h` (M7-TOOLCHAIN-004 slice 11 / PR #459)

| Symbol                | Signature                                 | Notes |
| --------------------- | ----------------------------------------- | ----- |
| `clib_inttypes_fmt`   | `const char *clib_inttypes_fmt(int which)` | drift anchor for `PRI*`/`SCN*` format-string macros (exact-width, least, fast, max, ptr) |

### `clib/iso646.h` (M7-TOOLCHAIN-004 slice)

| Symbol                   | Signature                              | Notes |
| ------------------------ | -------------------------------------- | ----- |
| `clib_iso646_eval`       | `int clib_iso646_eval(int op, int a, int b)` | drift anchor for `and`/`or`/`not`/`xor`/`bitand`/`bitor`/`compl` macros |
| `clib_iso646_op_count`   | `int clib_iso646_op_count(void)`       | reports number of pinned operator macros |

### `clib/stdalign.h` (M7-TOOLCHAIN-004 slice)

| Symbol                     | Signature                            | Notes |
| -------------------------- | ------------------------------------ | ----- |
| `clib_stdalign_eval`       | `unsigned long clib_stdalign_eval(int which)` | drift anchor for `alignas`/`alignof`/`__alignas_is_defined`/`__alignof_is_defined` |
| `clib_stdalign_op_count`   | `int clib_stdalign_op_count(void)`   | reports number of pinned C11 §7.15 macros |

### `clib/float.h` (M7-TOOLCHAIN-004 slice)

| Symbol                       | Signature                            | Notes |
| ---------------------------- | ------------------------------------ | ----- |
| `clib_float_eval_int`        | `long long clib_float_eval_int(int which)`     | drift anchor for integer-valued `<float.h>` macros (radix, mant_dig, exp ranges) |
| `clib_float_eval_fp`         | `double clib_float_eval_fp(int which)`         | drift anchor for floating-point-valued `<float.h>` macros (FLT/DBL/LDBL_*) |
| `clib_float_int_op_count`    | `int clib_float_int_op_count(void)`            | reports number of pinned integer macros |
| `clib_float_fp_op_count`     | `int clib_float_fp_op_count(void)`             | reports number of pinned FP macros |

### `clib/stdnoreturn.h` (M7-TOOLCHAIN-004 slice)

| Symbol                          | Signature                                | Notes |
| ------------------------------- | ---------------------------------------- | ----- |
| `clib_stdnoreturn_eval`         | `int clib_stdnoreturn_eval(int which)`   | drift anchor for the `noreturn` -> `_Noreturn` macro |
| `clib_stdnoreturn_op_count`     | `int clib_stdnoreturn_op_count(void)`    | reports number of pinned C11 §7.23 macros |
| `clib_stdnoreturn_loop_forever` | `_Noreturn void clib_stdnoreturn_loop_forever(void)` | exercises `_Noreturn` through the header alias |

### `clib/assert.h` (M7-TOOLCHAIN-004 slice)

| Symbol                       | Signature                                                    | Notes |
| ---------------------------- | ------------------------------------------------------------ | ----- |
| `__clib_assert_fail`         | `void __clib_assert_fail(const char *expr, const char *file, int line, const char *func)` | failure trampoline invoked by `assert()` |
| `clib_assert_set_handler`    | `void clib_assert_set_handler(clib_assert_handler_t fn)`     | install a custom handler (used by host tests) |

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
| `strtoll` | `long long strtoll(const char *s, char **end, int base)`           | slice 11 / PR #444; overflow clamps to `LLONG_MIN`/`LLONG_MAX` |
| `strtoull`| `unsigned long long strtoull(const char *s, char **end, int base)` | slice 11 / PR #444; overflow clamps to `ULLONG_MAX` |
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
| `strspn`  | `size_t strspn(const char *s, const char *accept)`                 | slice 12 / PR #445 |
| `strcspn` | `size_t strcspn(const char *s, const char *reject)`                | slice 12 / PR #445 |
| `strpbrk` | `char *strpbrk(const char *s, const char *accept)`                 | slice 12 / PR #445 |
| `strtok`  | `char *strtok(char *s, const char *delim)`                         | slice 12 / PR #445; static-state |
| `strtok_r`| `char *strtok_r(char *s, const char *delim, char **saveptr)`       | slice 12 / PR #445; re-entrant POSIX variant |

### `clib/stdio.h` (slice 8 / issue #447)

Freestanding `<stdio.h>` nucleus routed through a swappable
`clib_stdio_backend_t` function-pointer table. The embedder registers
the backend at init (`clib_stdio_init`); on-target it wraps
`os_fs_*` + `os_console_write`, on the host the test harness installs
a recorder shim.

| Symbol               | Signature                                                                       | Notes |
|----------------------|---------------------------------------------------------------------------------|-------|
| `clib_stdio_init`    | `int clib_stdio_init(const clib_stdio_backend_t *backend)`                      | embedder hook; rejects NULL backend |
| `clib_stdio_shutdown`| `void clib_stdio_shutdown(void)`                                                | resets FILE pool + backend |
| `stdin`              | `FILE *stdin`                                                                   | sentinel; outside FILE pool |
| `stdout`             | `FILE *stdout`                                                                  | sentinel; routes through `console_write` |
| `stderr`             | `FILE *stderr`                                                                  | sentinel; routes through `console_write` |
| `fopen`              | `FILE *fopen(const char *path, const char *mode)`                               | modes: `r`, `w`, `r+`, `w+`; rejects unknown |
| `fclose`             | `int fclose(FILE *fp)`                                                          | flushes; no-op on sentinels |
| `fread`              | `size_t fread(void *buf, size_t sz, size_t n, FILE *fp)`                        |  |
| `fwrite`             | `size_t fwrite(const void *buf, size_t sz, size_t n, FILE *fp)`                 |  |
| `fflush`             | `int fflush(FILE *fp)`                                                          | NULL flushes all open FILEs |
| `fputs`              | `int fputs(const char *s, FILE *fp)`                                            |  |
| `fputc`              | `int fputc(int c, FILE *fp)`                                                    |  |
| `fprintf`            | `int fprintf(FILE *fp, const char *fmt, ...)`                                   | minimal printf spec set |
| `vfprintf`           | `int vfprintf(FILE *fp, const char *fmt, va_list ap)`                           | 1 KiB stack buffer; returns full requested length |
| `printf`             | `int printf(const char *fmt, ...)`                                              | forwards to `vfprintf(stdout, ...)` |
| `feof`               | `int feof(FILE *fp)`                                                            |  |
| `ferror`             | `int ferror(FILE *fp)`                                                          |  |

printf spec set: `%s %d %i %u %x %p %c %% %ld %li %lu %lx`, optional
width (`%8d`), zero-pad (`%08d`), left-justify (`%-5s`). Unsupported
specs echo the literal `%...` sequence; `%p` always emits the `0x`
prefix.

## Canonical pin

The block below is the **machine-checkable** symbol pin. It MUST stay
byte-identical (modulo trailing newline) to
[`../../tests/data/clib_symbols.expected`](../../tests/data/clib_symbols.expected),
and every symbol named in any table above MUST appear here. The
`clib_symbol_drift` test enforces both invariants.

<!-- clib-symbols:begin -->
```
__clib_assert_fail
abs
atoi
bsearch
clib_assert_set_handler
clib_calloc
clib_float_eval_fp
clib_float_eval_int
clib_float_fp_op_count
clib_float_int_op_count
clib_free
clib_inttypes_fmt
clib_iso646_eval
clib_iso646_op_count
clib_limits_char_bit
clib_limits_check_value
clib_malloc
clib_malloc_get_stats
clib_malloc_init
clib_malloc_min_seed_bytes
clib_malloc_shutdown
clib_realloc
clib_stdalign_eval
clib_stdalign_op_count
clib_stdarg_sum_ints
clib_stdarg_sum_ints_via_copy
clib_stdbool_false_value
clib_stdbool_feature_macro_value
clib_stdbool_sizeof_bool
clib_stdbool_true_value
clib_stddef_offsetof_probe
clib_stddef_sizeof
clib_stdint_maxof
clib_stdint_sizeof
clib_stdio_init
clib_stdio_shutdown
clib_stdnoreturn_eval
clib_stdnoreturn_loop_forever
clib_stdnoreturn_op_count
clib_strerror
errno
fclose
feof
ferror
fflush
fopen
fprintf
fputc
fputs
fread
fwrite
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
printf
qsort
stderr
stdin
stdout
strcat
strchr
strcmp
strcpy
strcspn
strlen
strncat
strncmp
strncpy
strnlen
strpbrk
strrchr
strspn
strstr
strtok
strtok_r
strtol
strtoll
strtoul
strtoull
tolower
toupper
vfprintf
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
