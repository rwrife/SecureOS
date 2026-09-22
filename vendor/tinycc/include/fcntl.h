/*
 * vendor/tinycc/include/fcntl.h — freestanding <fcntl.h> shim for the
 * SecureOS TinyCC build (issue #766 / #408 Phase 3, first-compile slice).
 *
 * TinyCC's tcc.h includes <fcntl.h> for the open() flag constants. The
 * SecureOS fd surface itself lives in
 * `user/libs/clib/include/clib/posix_fd.h` (issue #538), which already
 * defines the O_* constants it supports; those definitions are guarded
 * with #ifndef, so this shim must agree with them byte-for-value. The
 * values below are copied verbatim from posix_fd.h — changing them here
 * without posix_fd.h is drift the `tinycc_freestanding_compile` gate
 * catches by compiling libtcc against the real headers.
 *
 * Discovered through `-I vendor/tinycc/include` with `-nostdlibinc`.
 */

#ifndef SECUREOS_TINYCC_SHIM_FCNTL_H
#define SECUREOS_TINYCC_SHIM_FCNTL_H

/* Values MUST match user/libs/clib/include/clib/posix_fd.h exactly. */
#ifndef O_RDONLY
#define O_RDONLY  0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY  0x0001
#endif
#ifndef O_RDWR
#define O_RDWR    0x0002
#endif
#ifndef O_ACCMODE
#define O_ACCMODE 0x0003
#endif
#ifndef O_CREAT
#define O_CREAT   0x0040
#endif
#ifndef O_TRUNC
#define O_TRUNC   0x0200
#endif
#ifndef O_APPEND
#define O_APPEND  0x0400
#endif

#endif /* SECUREOS_TINYCC_SHIM_FCNTL_H */
