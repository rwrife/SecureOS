/*
 * vendor/tinycc/include/time.h — freestanding <time.h> shim for the
 * SecureOS TinyCC build (issue #766 / #408 Phase 3, first-compile slice).
 *
 * TinyCC's tcc.h includes <time.h> for `time()` / `localtime()` used by
 * the __DATE__/__TIME__ preprocessor macros (tccpp.c). The SecureOS
 * deterministic implementations and the `time_t` / `struct tm` types
 * live in `user/libs/clib/include/clib/runtime_compat.h` (issue #539);
 * this shim only forwards to it.
 *
 * Discovered through `-I vendor/tinycc/include`.
 */

#ifndef SECUREOS_TINYCC_SHIM_TIME_H
#define SECUREOS_TINYCC_SHIM_TIME_H

#include <runtime_compat.h>

#endif /* SECUREOS_TINYCC_SHIM_TIME_H */
