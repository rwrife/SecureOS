/**
 * @file src/runtime_compat.c
 * @brief TinyCC hosted-libc compatibility shims for SecureOS `libclib.a`.
 *
 * Issue #539 (M7-TOOLCHAIN-005): the freestanding TinyCC port (#408) still
 * references a small hosted runtime surface. This TU provides deterministic,
 * userland-only implementations so those symbols resolve without introducing
 * a hosted libc or a dynamic loader requirement.
 *
 * Provided behavior:
 *   - `free` / `realloc`: forward to `clib_free` / `clib_realloc`.
 *   - `getcwd`: deterministic fixed cwd (`/apps/dev`) with bounds checks.
 *   - `getenv`: always returns NULL (no env-var surface in SecureOS v0).
 *   - `time` / `localtime`: deterministic fixed timestamp and broken-down
 *     time for reproducible builds.
 *   - `realpath`: deterministic passthrough (no filesystem canonicalization).
 *   - `dlopen` / `dlsym`: explicit unsupported stubs returning NULL.
 *
 * Called by:
 *   - TinyCC freestanding objects linked against `libclib.a`.
 *   - Any userland binary that links clib and references these hosted-shape
 *     symbols directly.
 */

#include "../include/clib/runtime_compat.h"

#include "../include/clib/errno.h"
#include "../include/clib/malloc.h"

#include <stddef.h>

#define CLIB_RUNTIME_FIXED_CWD "/apps/dev"
#define CLIB_RUNTIME_FIXED_EPOCH ((time_t)1704067200L) /* 2024-01-01T00:00:00Z */

static struct tm g_fixed_tm = {
    .tm_sec = 0,
    .tm_min = 0,
    .tm_hour = 0,
    .tm_mday = 1,
    .tm_mon = 0,
    .tm_year = 124, /* years since 1900: 2024 */
    .tm_wday = 1,   /* Monday */
    .tm_yday = 0,
    .tm_isdst = 0,
};

static size_t local_strlen(const char *s) {
  size_t n = 0;
  if (!s) {
    return 0;
  }
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

void free(void *ptr) {
  clib_free(ptr);
}

void *realloc(void *ptr, size_t size) {
  return clib_realloc(ptr, size);
}

char *getcwd(char *buf, size_t size) {
  const char *cwd = CLIB_RUNTIME_FIXED_CWD;
  size_t len = local_strlen(cwd);

  if (!buf || size == 0u) {
    errno = EINVAL;
    return NULL;
  }

  if (size <= len) {
    errno = ERANGE;
    return NULL;
  }

  for (size_t i = 0; i <= len; ++i) {
    buf[i] = cwd[i];
  }
  return buf;
}

char *getenv(const char *name) {
  (void)name;
  return NULL;
}

time_t time(time_t *tloc) {
  if (tloc) {
    *tloc = CLIB_RUNTIME_FIXED_EPOCH;
  }
  return CLIB_RUNTIME_FIXED_EPOCH;
}

struct tm *localtime(const time_t *timer) {
  if (!timer) {
    errno = EINVAL;
    return NULL;
  }
  return &g_fixed_tm;
}

char *realpath(const char *path, char *resolved_path) {
  if (!path || path[0] == '\0') {
    errno = EINVAL;
    return NULL;
  }

  if (!resolved_path) {
    return (char *)path;
  }

  size_t i = 0;
  do {
    resolved_path[i] = path[i];
  } while (path[i++] != '\0');

  return resolved_path;
}

void *dlopen(const char *filename, int flags) {
  (void)filename;
  (void)flags;
  errno = ENOTSUP;
  return NULL;
}

void *dlsym(void *handle, const char *symbol) {
  (void)handle;
  (void)symbol;
  errno = ENOTSUP;
  return NULL;
}
