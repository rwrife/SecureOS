/*
 * vendor/tinycc/include/tcc-compat.h — TinyCC call-site compatibility
 * declarations for the SecureOS freestanding build (issue #766 / #408
 * Phase 3, first-compile slice).
 *
 * Passed to every libtcc TU compile with `-include` (alongside the
 * config-secureos.h redirect). It declares the handful of libc symbols
 * the pinned `TCC_ALL_SRCS` set calls that clib does NOT expose under
 * the canonical name TinyCC uses:
 *
 *   - `fdopen(int fd, const char *mode)` — tccelf.c's output-file path.
 *     clib's FILE pool is path-based (no fd bridge); shipping a real
 *     fdopen is future work tracked under #766. Declared here so the
 *     translation unit compiles; the link step (also #766 follow-up)
 *     fails loudly if the code path is ever reached without the shim.
 *
 *   - `strerror(int)` — tccelf.c diagnostics. clib ships the bounded
 *     variant as `clib_strerror()` (deliberate naming, issue #452);
 *     the eventual link layer aliases the plain name to it.
 *
 *   - `strtod(const char*, char**)` — tccpp.c double-literal parsing.
 *     clib has no float conversion surface yet; declaration-only here,
 *     same link-contract note as fdopen. (Not a new clib symbol: it is
 *     intentionally NOT added to tests/data/clib_symbols.expected.)
 *
 *   - `qsort` / clib's other split headers — TinyCC gets its ISO
 *     headers through clib via the `-I` graph; qsort is the one symbol
 *     TinyCC calls whose declaration lives in clib/qsort.h rather than
 *     stdlib.h.
 *
 * Everything declared here is TinyCC-build glue and lives OUT of clib's
 * public ABI (docs/abi/clib-symbols.md + the clib_symbol_drift pin stay
 * untouched by this file).
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
