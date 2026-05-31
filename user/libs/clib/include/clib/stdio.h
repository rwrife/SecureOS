/**
 * @file include/clib/stdio.h
 * @brief Freestanding `<stdio.h>` nucleus for the in-OS toolchain libc
 *        (issue #447 / #407 — M7-TOOLCHAIN-004 slice 8, plan
 *         `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * Purpose
 * -------
 * TinyCC (#408) and the `cc` driver app (#409) need a freestanding
 * `stdio` to read source files, persist `.sof` output, and emit
 * diagnostics to the console. This slice ships the minimum subset
 * those callers exercise:
 *
 *   - `FILE`, `stdin`, `stdout`, `stderr`
 *   - `fopen` / `fclose` / `fread` / `fwrite` / `fflush`
 *   - `fputs` / `fputc`
 *   - `fprintf` / `vfprintf` / `printf`
 *   - format specifiers TinyCC and `cc` actually emit:
 *       `%s %d %u %x %p %c %%`
 *       `%ld %lu` for `long` ints
 *       optional width: `%8d`
 *       zero-pad: `%08d`
 *
 * What we deliberately do NOT ship (per #447 "Out of scope")
 * ----------------------------------------------------------
 *   - `scanf` family (TinyCC does not require it)
 *   - floating-point conversion specifiers (`%f %g %e`)
 *   - locale, wide chars, multibyte
 *   - threads / `flockfile`
 *   - positional argument specifiers (`%1$s`)
 *
 * Backend abstraction (host testability + freestanding contract)
 * --------------------------------------------------------------
 * `user/libs/clib/` MUST NOT include `<secureos_api.h>` or any
 * kernel header (same rule the str/mem/ctype/qsort/stdlib/errno/
 * stdarg slices follow). To stay clean we route every I/O call
 * through a `clib_stdio_backend_t` function-pointer table the
 * embedder registers at init time:
 *
 *   - on-target: the SDK crt0 / app init layer fills the table with
 *     thunks to `os_fs_read_file` / `os_fs_write_file` /
 *     `os_console_write` (out of scope for this slice — wires in
 *     with the embedder side of #404/#411).
 *   - host tests: `tests/clib_stdio_test.c` registers a recorder
 *     backend that captures bytes into an in-memory buffer table,
 *     pins exact output, and asserts `fprintf(stderr, ...)` routes
 *     to the console sink (not the file sink).
 *
 * The backend table is the entire seam between `libclib.a` and the
 * OS — TinyCC links against this header and never sees a syscall.
 *
 * Memory
 * ------
 * Per-FILE write buffers grow via `clib_realloc` from the existing
 * heap (#404 / PR #412). Callers MUST have already initialised the
 * allocator via `clib_malloc_init(...)` before any `fopen("w"|"a")`
 * call. `stdin`/`stdout`/`stderr` are static FILEs and do NOT touch
 * the heap.
 *
 * ABI
 * ---
 * None. Purely additive userland surface; no `OS_ABI_VERSION` bump.
 *
 * Issue: #447. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P3).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_STDIO_H
#define SECUREOS_USER_LIBS_CLIB_STDIO_H

#include <stddef.h>

#include "stdarg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Backend status type ------------------------------------------------
 *
 * Mirrors the shape (not the identity) of `os_status_t` so the embedder
 * can pass values through without casting. We deliberately do NOT
 * include `<secureos_api.h>` here — keeping clib freestanding is a
 * #407 contract bullet, and the EOF / error sentinels below let
 * `stdio`'s public surface stay status-agnostic.
 */
typedef enum {
  CLIB_STDIO_OK     = 0,
  CLIB_STDIO_DENIED = 1,
  CLIB_STDIO_ERROR  = 2
} clib_stdio_status_t;

/* ----- Backend ops --------------------------------------------------------
 *
 * `read_file(path, buf, *io_size)`:
 *   On entry `*io_size` is the buffer capacity. On `CLIB_STDIO_OK`
 *   the callee MUST update `*io_size` to the number of bytes copied
 *   into `buf`. If the file is larger than the buffer the callee
 *   MAY truncate (and stdio.c will report short read).
 *
 * `write_file(path, content, size, append)`:
 *   Writes `size` bytes from `content` to `path`. `append != 0`
 *   appends; `append == 0` overwrites. The on-target FS path (PR
 *   #411) supports multi-cluster writes, so `size` may exceed any
 *   single-cluster bound.
 *
 * `console_write(message)`:
 *   NUL-terminated string sink. stdio.c always passes a NUL-
 *   terminated buffer; the recorder shim asserts on this.
 *
 * All three may be NULL: the corresponding stdio call returns 0 /
 * EOF / a short count rather than crashing. Tests rely on this for
 * the `stderr_routes_to_console` assertion (file backend may be
 * unset while console is mocked, and `fprintf(stderr,...)` must
 * still produce output).
 */
typedef struct clib_stdio_backend {
  clib_stdio_status_t (*read_file)(const char *path,
                                   char       *buf,
                                   size_t     *io_size,
                                   void       *ctx);
  clib_stdio_status_t (*write_file)(const char *path,
                                    const char *content,
                                    size_t      size,
                                    int         append,
                                    void       *ctx);
  clib_stdio_status_t (*console_write)(const char *message,
                                       void       *ctx);
  void *ctx;
} clib_stdio_backend_t;

/*
 * Register the backend table. `backend == NULL` clears registration
 * (all stdio calls become deny/short-count no-ops — useful for the
 * `defensive_no_backend` host case).
 */
void clib_stdio_init(const clib_stdio_backend_t *backend);

/*
 * Reset all dynamic stdio state (closes any leaked FILE handles
 * opened via `fopen`, frees their buffers, clears the backend
 * registration). Safe to call multiple times. The host test calls
 * this between sub-cases to prove the `clib_stdio_init` /
 * `clib_stdio_shutdown` pair has no cross-case leakage.
 */
void clib_stdio_shutdown(void);

/* ----- FILE & standard streams ------------------------------------------ */

typedef struct clib_FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#ifndef EOF
#define EOF (-1)
#endif

/*
 * File access modes accepted by `fopen`:
 *   "r"   — read.  fopen reads the entire file into an internal
 *                  buffer at open-time (one backend.read_file call).
 *   "w"   — write. fopen succeeds without backend interaction; on
 *                  fclose / fflush we send the accumulated buffer
 *                  via backend.write_file(append=0). Each fflush
 *                  flips subsequent flushes to append mode so we
 *                  never lose earlier bytes.
 *   "a"   — append. Same as "w" but with append=1 from the first
 *                  flush.
 * Any other mode string returns NULL.
 */
FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *fp);

size_t fread(void *buf, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *fp);
int    fflush(FILE *fp);

int    fputs(const char *s, FILE *fp);
int    fputc(int c, FILE *fp);

int    fprintf(FILE *fp, const char *fmt, ...);
int    vfprintf(FILE *fp, const char *fmt, va_list ap);
int    printf(const char *fmt, ...);

/* `feof` / `ferror`: TinyCC tests these on its source-file reads. */
int feof(FILE *fp);
int ferror(FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_STDIO_H */
