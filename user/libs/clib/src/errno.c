/**
 * @file src/errno.c
 * @brief Freestanding userland <errno.h> implementation for user/libs/clib.
 *
 * Issue #407 / M7-TOOLCHAIN-004 slice 5
 * (plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Ships:
 *   - the writable `int errno;` global (single-threaded userland at
 *     OS_ABI_VERSION=0, so no `__errno_location` indirection), and
 *   - `clib_strerror(int)` — bounded ASCII description, never NULL,
 *     table-backed pointer the caller must not free.
 *
 * No libc, no kernel includes, no syscalls. Compiles under
 * -ffreestanding. The bounds-checked switch deliberately avoids a
 * sparse `[]` array so an unrecognised errnum cannot index into
 * uninitialised storage.
 */

#include "../include/clib/errno.h"

/*
 * Writable, zero-initialised by the BSS contract. Callers (e.g. a
 * future strtol/strtoul slice setting ERANGE on overflow) write
 * directly to this symbol. The symbol_set_pinned test asserts the
 * address is stable across reads — i.e., no accidental TLS storage
 * class snuck in.
 */
int errno = 0;

const char *clib_strerror(int errnum) {
  switch (errnum) {
    case 0:         return "Success";
    case EPERM:     return "Operation not permitted";
    case ENOENT:    return "No such file or directory";
    case EIO:       return "I/O error";
    case EBADF:     return "Bad file descriptor";
    case ENOMEM:    return "Out of memory";
    case EACCES:    return "Permission denied";
    case EFAULT:    return "Bad address";
    case EBUSY:     return "Device or resource busy";
    case EEXIST:    return "File exists";
    case ENOTDIR:   return "Not a directory";
    case EISDIR:    return "Is a directory";
    case EINVAL:    return "Invalid argument";
    case ENFILE:    return "Too many open files in system";
    case EMFILE:    return "Too many open files";
    case ENOSPC:    return "No space left on device";
    case ESPIPE:    return "Illegal seek";
    case EROFS:     return "Read-only file system";
    case ERANGE:    return "Numerical result out of range";
    case ENOSYS:    return "Function not implemented";
    case ENOTSUP:   return "Operation not supported";
    case EOVERFLOW: return "Value too large for defined data type";
    default:        return "Unknown error";
  }
}
