/*
 * include/clib/runtime_compat.h
 *
 * M7-TOOLCHAIN-005 / issue #539 compatibility surface for the freestanding
 * TinyCC port (#408). This header centralizes the remaining hosted-libc-shaped
 * symbols TinyCC references (`realloc`, `free`, `getcwd`, `getenv`, `time`,
 * `localtime`, `realpath`, `dlopen`, `dlsym`) so `libclib.a` can satisfy the
 * link surface without pulling in a hosted runtime.
 *
 * Scope and intent:
 *   - deterministic stubs for environment/time/path helpers;
 *   - plain-name aliases/forwarders to existing clib allocator symbols;
 *   - explicit no-op stubs for optional JIT loader hooks (`dlopen`, `dlsym`).
 *
 * This is a userland-only additive header. No kernel ABI opcodes or
 * capabilities are introduced.
 */

#ifndef SECUREOS_USER_LIBS_CLIB_RUNTIME_COMPAT_H
#define SECUREOS_USER_LIBS_CLIB_RUNTIME_COMPAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SECUREOS_CLIB_TIME_T_DEFINED
typedef long time_t;
#define SECUREOS_CLIB_TIME_T_DEFINED 1
#endif

#ifndef SECUREOS_CLIB_TM_DEFINED
#define SECUREOS_CLIB_TM_DEFINED 1
struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};
#endif

void  free(void *ptr);
void *realloc(void *ptr, size_t size);

char *getcwd(char *buf, size_t size);
char *getenv(const char *name);

time_t     time(time_t *tloc);
struct tm *localtime(const time_t *timer);

char *realpath(const char *path, char *resolved_path);

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *symbol);

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_USER_LIBS_CLIB_RUNTIME_COMPAT_H */
