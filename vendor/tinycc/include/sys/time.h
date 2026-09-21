/*
 * vendor/tinycc/include/sys/time.h — empty freestanding <sys/time.h>
 * shim for the SecureOS TinyCC build (issue #766 / #408 Phase 3,
 * first-compile slice).
 *
 * TinyCC's tcc.h includes <sys/time.h> but the in-scope `TCC_ALL_SRCS`
 * set uses nothing from it (`gettimeofday` appears only in tccrun.c,
 * which is excluded from the SecureOS build by Makefile.secureos). The
 * empty TU exists so the include graph resolves under `-nostdlibinc`.
 *
 * Discovered through `-I vendor/tinycc/include`.
 */

#ifndef SECUREOS_TINYCC_SHIM_SYS_TIME_H
#define SECUREOS_TINYCC_SHIM_SYS_TIME_H

#endif /* SECUREOS_TINYCC_SHIM_SYS_TIME_H */
