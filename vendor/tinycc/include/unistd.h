/*
 * vendor/tinycc/include/unistd.h — freestanding <unistd.h> shim for the
 * SecureOS TinyCC build (issue #766 / #408 Phase 3, first-compile slice).
 *
 * TinyCC's tcc.h includes <unistd.h> for the POSIX fd surface
 * (open/close/read/write/lseek/unlink, SEEK_*). The SecureOS
 * implementation and declarations live in
 * `user/libs/clib/include/clib/posix_fd.h` (issue #538); this shim only
 * forwards to it so `#include <unistd.h>` resolves in the freestanding
 * include graph (`-nostdlibinc -I vendor/tinycc/include`).
 *
 * Discovered through `-I vendor/tinycc/include`.
 */

#ifndef SECUREOS_TINYCC_SHIM_UNISTD_H
#define SECUREOS_TINYCC_SHIM_UNISTD_H

#include <posix_fd.h>

#endif /* SECUREOS_TINYCC_SHIM_UNISTD_H */
