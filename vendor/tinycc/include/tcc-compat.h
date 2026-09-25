/*
 * vendor/tinycc/include/tcc-compat.h — TinyCC call-site compatibility
 * declarations for the SecureOS freestanding build (issue #766 / #408
 * Phase 3, first-compile slice).
 *
 * Passed to every libtcc TU compile with `-include` (alongside the
 * config-secureos.h redirect). It declares the handful of libc symbols
 * the pinned `TCC_ALL_SRCS` set calls; since the #766 link slice all of
 * them have real clib implementations:
 *
 *   - `fdopen(int fd, const char *mode)` — tccelf.c's output-file path.
 *     clib ships a real implementation in src/stdio.c (issue #766 link
 *     slice): it adopts the descriptor, owns it through fclose, and
 *     routes persistence through the stdio backend so the fd's stale
 *     snapshot can never truncate the link output.
 *
 *   - `strerror(int)` — tccelf.c diagnostics. clib ships the bounded
 *     variant as `clib_strerror()` (deliberate naming, issue #452) and
 *     plain `strerror` as a linker alias to it (src/errno.c, #766).
 *
 *   - `strtod(const char*, char**)` — tccpp.c double-literal parsing.
 *     clib ships deterministic digit-accumulation conversions in
 *     src/strtod.c (strtod/strtof/strtold/ldexpl, #766) declared in
 *     clib/stdlib.h.
 *
 *   - `qsort` / clib's other split headers — TinyCC gets its ISO
 *     headers through clib via the `-I` graph; qsort is the one symbol
 *     TinyCC calls whose declaration lives in clib/qsort.h rather than
 *     stdlib.h.
 *
 * The declarations below are kept as a belt-and-suspenders TinyCC
 * call-site surface (they must stay signature-identical to the clib
 * headers, or the compile fails on redeclaration mismatch).
 *
 * Everything declared here is TinyCC-build glue; the implementations
 * live in clib and are pinned by docs/abi/clib-symbols.md + the
 * clib_symbol_drift gate.
 *
 * Discovered through `-I vendor/tinycc/include` + `-include`.
 */

#ifndef SECUREOS_TINYCC_COMPAT_H
#define SECUREOS_TINYCC_COMPAT_H

#include <qsort.h>   /* clib qsort — TinyCC symtab sorts call it */
#include <stdio.h>   /* clib stdio (FILE, fopen, fwrite, ...)      */
#include <stdlib.h>  /* clib stdlib (strtol family, abs, ...)      */

#ifdef __cplusplus
extern "C" {
#endif

/* See header comment: these three are TinyCC call-site declarations, not
 * new clib ABI. Link is established by the #766 follow-up slice. */
FILE *fdopen(int fd, const char *mode);
const char *strerror(int errnum);
double strtod(const char *nptr, char **endptr);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_TINYCC_COMPAT_H */
