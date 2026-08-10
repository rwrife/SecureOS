/*
 * include/clib/posix_fd.h
 * Freestanding POSIX-style file-descriptor nucleus for user/libs/clib
 * (M7-TOOLCHAIN-005 sub-slice, issue #538).
 *
 * Purpose:
 *   TinyCC's freestanding build path references the POSIX fd surface
 *   (`open`, `close`, `read`, `lseek`, `unlink`). This header exposes a
 *   minimal, additive declaration set so those symbols resolve from
 *   `libclib.a` without introducing any kernel-header dependency.
 *
 * Containment:
 *   - No hosted libc headers beyond <stddef.h>.
 *   - Types are defined locally (`ssize_t`, `off_t`) to keep the surface
 *     freestanding.
 *   - Flag constants intentionally cover only the subset this slice uses.
 *
 * Notes:
 *   - Semantics are documented in src/posix_fd.c.
 *   - This is an additive userland surface at OS_ABI_VERSION=0.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_POSIX_FD_H
#define SECUREOS_USER_LIBS_CLIB_POSIX_FD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CLIB_SSIZE_T_DEFINED
#define CLIB_SSIZE_T_DEFINED 1
typedef long ssize_t;
#endif

#ifndef CLIB_OFF_T_DEFINED
#define CLIB_OFF_T_DEFINED 1
typedef long off_t;
#endif

#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif
#ifndef O_RDWR
#define O_RDWR   0x0002
#endif
#ifndef O_ACCMODE
#define O_ACCMODE 0x0003
#endif
#ifndef O_CREAT
#define O_CREAT  0x0040
#endif
#ifndef O_TRUNC
#define O_TRUNC  0x0200
#endif
#ifndef O_APPEND
#define O_APPEND 0x0400
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_POSIX_FD_H */
