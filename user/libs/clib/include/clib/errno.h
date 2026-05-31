/*
 * include/clib/errno.h
 * Freestanding userland <errno.h> for user/libs/clib
 * (M7-TOOLCHAIN-004 / issue #407 slice 5, plan
 *  plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Purpose
 *   PR #428's freestanding `stdlib.c` documents in its header that the
 *   in-OS toolchain libc has "no errno" — overflow paths in `strtol` /
 *   `strtoul` clamp silently because the canonical POSIX
 *   `errno = ERANGE` assignment has nowhere to land. TinyCC's driver
 *   and a handful of `tcc.c` numeric paths read `errno` directly after
 *   calling `strtol` to distinguish a clean clamp from an actual
 *   overflow. Filing this slice now lands the symbol + macro family
 *   so the M7-TOOLCHAIN-005 TinyCC port (issue #408) does not hit a
 *   link-error on `errno` / `ERANGE` / `EINVAL` / `ENOMEM`, and so
 *   the slice-4 stdlib clamp paths can be flipped from "silent" to
 *   "errno = ERANGE" without touching the symbol surface (landed in
 *   the errno-on-overflow follow-up that wires `errno = ERANGE` into
 *   `strtol` / `strtoul` / `strtoll` / `strtoull` and `errno = EINVAL`
 *   on a bad `base`).
 *
 * Why it lives in this slice
 *   Same shape as the str/mem, ctype, qsort, and stdlib slices: pure
 *   userland, no kernel includes, no syscalls, different header,
 *   different source file, different `symbol_set_pinned` sub-marker.
 *   Lands in parallel with the slice-4 `stdlib` PR (#428) — neither
 *   slice writes to the other's symbol surface.
 *
 * Containment
 *   Freestanding. Provides a plain `int errno;` global; no per-thread
 *   storage (SecureOS userland is single-threaded at OS_ABI_VERSION=0).
 *   No `errno_t`, no `strerror_l`, no locale.
 *
 *   Macro values match the canonical Linux / musl numbering for the
 *   symbols TinyCC and a sane libc-consumer touch. Numbers are stable
 *   under this freestanding contract — see `symbol_set_pinned` in the
 *   host test for the drift guard.
 *
 * Shipped surface (pinned by `symbol_set_pinned`)
 *   Storage:
 *     - `int errno;`   -- writable global (no `__errno_location`).
 *   Macros (value = literal int, matches musl/Linux ABI for portability):
 *     - EPERM, ENOENT, EIO, EBADF, ENOMEM, EACCES, EFAULT, EBUSY,
 *       EEXIST, ENOTDIR, EISDIR, EINVAL, ENFILE, EMFILE, ENOSPC,
 *       ESPIPE, EROFS, ERANGE, ENOSYS, ENOTSUP, EOVERFLOW
 *   Helper:
 *     - `const char *clib_strerror(int errnum);`  -- bounded ASCII
 *       descriptions, no allocation, returns "Unknown error" for
 *       unrecognised codes (and never NULL, matching glibc).
 *
 * Why not real `strerror`?
 *   Canonical `strerror` is allowed to share a static buffer with the
 *   locale subsystem in hosted libc; we have no locale and want the
 *   contract spelled out, so the slice ships an explicitly bounded
 *   `clib_strerror`. A future slice may alias `strerror` to it once
 *   TinyCC requires the canonical spelling.
 *
 * Interactions
 *   - src/errno.c                 -- definition of `errno` + table.
 *   - tests/clib_errno_test.c     -- host unit test (compiled
 *                                    -fno-builtin, no system <errno.h>
 *                                    so the assertions exercise OUR
 *                                    macros/global rather than glibc's).
 *   - build/scripts/test_clib_errno.sh -- TEST:PASS:clib_errno driver.
 *
 * Issue: 407. Plan: plans/2026-05-28-in-os-toolchain-self-hosting.md (P3).
 */

#ifndef SECUREOS_USER_LIBS_CLIB_ERRNO_H
#define SECUREOS_USER_LIBS_CLIB_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Writable global. Single-threaded userland at v0, so no
 * per-thread indirection. Hosted libcs hide this behind
 * `*__errno_location()`; we don't, and the symbol_set_pinned test
 * asserts the address is stable across reads (no accidental TLS).
 */
extern int errno;

/*
 * Error numbers — chosen to match musl / Linux so on-target wrappers
 * around `os_*` syscalls can translate their `OS_STATUS_*` enum to a
 * familiar POSIX-shaped int without an extra mapping table. Numbers
 * are part of the symbol_set_pinned drift contract; bumping any of
 * them is an ABI break inside the libc nucleus.
 */
#define EPERM       1   /* Operation not permitted */
#define ENOENT      2   /* No such file or directory */
#define EIO         5   /* I/O error */
#define EBADF       9   /* Bad file descriptor */
#define ENOMEM      12  /* Out of memory */
#define EACCES      13  /* Permission denied */
#define EFAULT      14  /* Bad address */
#define EBUSY       16  /* Device or resource busy */
#define EEXIST      17  /* File exists */
#define ENOTDIR     20  /* Not a directory */
#define EISDIR      21  /* Is a directory */
#define EINVAL      22  /* Invalid argument */
#define ENFILE      23  /* Too many open files in system */
#define EMFILE      24  /* Too many open files (per-process) */
#define ENOSPC      28  /* No space left on device */
#define ESPIPE      29  /* Illegal seek */
#define EROFS       30  /* Read-only file system */
#define ERANGE      34  /* Math result not representable */
#define ENOSYS      38  /* Function not implemented */
#define ENOTSUP     95  /* Operation not supported */
#define EOVERFLOW   75  /* Value too large for defined data type */

/*
 * Bounded ASCII description. Never NULL. Caller MUST NOT free the
 * return value (table-backed pointer). Unknown codes return the
 * literal string "Unknown error".
 */
const char *clib_strerror(int errnum);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_ERRNO_H */
