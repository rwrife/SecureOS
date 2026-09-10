/**
 * @file tests/clib_tinycc_link_surface_test.c
 * @brief Host gate for the TinyCC runtime-compat link surface (issue #539).
 *
 * This test is launched by build/scripts/test_clib_tinycc_link_surface.sh and
 * validates that the remaining hosted-libc-shaped symbols required by the
 * freestanding TinyCC port resolve through libclib objects with deterministic
 * runtime behavior.
 *
 * It explicitly pins the #539 symbol set:
 *   realloc, free, sprintf, exit, time, localtime,
 *   getcwd, getenv, realpath, dlopen, dlsym.
 */

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "clib/errno.h"
#include "clib/malloc.h"
#include "clib/runtime_compat.h"
#include "clib/stdlib.h"

/* `sprintf` is exported by user/libs/clib/src/stdio.c and shares the libc
 * symbol name. Bind an explicit alias so we can exercise the clib symbol
 * without including clib/stdio.h (which collides with host FILE symbols). */
int clib_sprintf(char *buf, const char *fmt, ...) __asm__("sprintf");

static int g_failures = 0;

static void emit_line(const char *line) {
  (void)write(1, line, strlen(line));
  (void)write(1, "\n", 1);
}

static void record_check(int ok, const char *name) {
  char marker[160];
  const char *prefix = ok ? "TEST:PASS:clib_tinycc_link_surface:" :
                            "TEST:FAIL:clib_tinycc_link_surface:";
  clib_sprintf(marker, "%s%s", prefix, name);
  emit_line(marker);
  if (!ok) {
    g_failures++;
  }
}

static void test_symbol_addresses(void) {
  /* Typed references force linker resolution against clib objects. */
  void *(*sym_realloc)(void *, size_t) = realloc;
  void (*sym_free)(void *) = free;
  int (*sym_sprintf)(char *, const char *, ...) = clib_sprintf;
  void (*sym_exit)(int) = exit;
  time_t (*sym_time)(time_t *) = time;
  struct tm *(*sym_localtime)(const time_t *) = localtime;
  char *(*sym_getcwd)(char *, size_t) = getcwd;
  char *(*sym_getenv)(const char *) = getenv;
  char *(*sym_realpath)(const char *, char *) = realpath;
  void *(*sym_dlopen)(const char *, int) = dlopen;
  void *(*sym_dlsym)(void *, const char *) = dlsym;

  int ok = sym_realloc && sym_free && sym_sprintf && sym_exit && sym_time &&
           sym_localtime && sym_getcwd && sym_getenv && sym_realpath &&
           sym_dlopen && sym_dlsym;
  record_check(ok, "symbol_set_pinned");
}

static uintptr_t align16_uintptr(uintptr_t value) {
  return (value + 15u) & ~(uintptr_t)15u;
}

static void test_allocator_forwarders(void) {
  unsigned char arena_raw[4096 + 16];
  uintptr_t aligned = align16_uintptr((uintptr_t)arena_raw);
  unsigned char *arena = (unsigned char *)aligned;
  size_t arena_size = sizeof(arena_raw) - (size_t)(aligned - (uintptr_t)arena_raw);
  arena_size &= ~(size_t)15u;

  int init_rc = clib_malloc_init(arena, arena_size, NULL, NULL);
  if (init_rc != 0) {
    record_check(0, "allocator_init");
    return;
  }
  record_check(1, "allocator_init");

  unsigned char *buf = (unsigned char *)realloc(NULL, 32);
  if (!buf) {
    record_check(0, "realloc_allocates");
    clib_malloc_shutdown();
    return;
  }
  memset(buf, 0xA5, 32);
  record_check(1, "realloc_allocates");

  unsigned char *grown = (unsigned char *)realloc(buf, 64);
  if (!grown) {
    record_check(0, "realloc_grows");
    clib_malloc_shutdown();
    return;
  }
  record_check(1, "realloc_grows");

  int preserved = 1;
  for (int i = 0; i < 32; ++i) {
    if (grown[i] != 0xA5) {
      preserved = 0;
      break;
    }
  }
  record_check(preserved, "realloc_preserves_prefix");

  free(grown);
  record_check(1, "free_forwarder_invoked");
  clib_malloc_shutdown();
}

static void test_runtime_compat_determinism(void) {
  char cwd[32] = {0};
  errno = 0;
  char *cwd_rc = getcwd(cwd, sizeof(cwd));
  record_check(cwd_rc == cwd && strcmp(cwd, "/apps/dev") == 0,
               "getcwd_fixed_value");

  char tiny[4] = {0};
  errno = 0;
  record_check(getcwd(tiny, sizeof(tiny)) == NULL && errno == ERANGE,
               "getcwd_small_buffer_erange");

  record_check(getenv("PATH") == NULL, "getenv_null_stub");

  time_t loc = 0;
  time_t now = time(&loc);
  record_check(now == (time_t)1704067200L && loc == (time_t)1704067200L,
               "time_fixed_epoch");

  errno = 0;
  record_check(localtime(NULL) == NULL && errno == EINVAL,
               "localtime_null_einval");

  struct tm *tmv = localtime(&now);
  record_check(tmv != NULL && tmv->tm_year == 124 && tmv->tm_mon == 0 &&
                   tmv->tm_mday == 1 && tmv->tm_hour == 0,
               "localtime_fixed_breakdown");

  const char *path = "/apps/dev/hello.c";
  char resolved[64] = {0};
  record_check(realpath(path, NULL) == path, "realpath_passthrough_pointer");
  record_check(realpath(path, resolved) == resolved &&
                   strcmp(resolved, path) == 0,
               "realpath_copy");

  errno = 0;
  record_check(dlopen("ignored", 0) == NULL && errno == ENOTSUP,
               "dlopen_enotsup");

  errno = 0;
  record_check(dlsym((void *)0x1, "sym") == NULL && errno == ENOTSUP,
               "dlsym_enotsup");
}

static void test_sprintf_surface(void) {
  char out[32] = {0};
  int n = clib_sprintf(out, "x=%d", 7);
  record_check(n == 3 && strcmp(out, "x=7") == 0, "sprintf_works");
}

int main(void) {
  test_symbol_addresses();
  test_allocator_forwarders();
  test_runtime_compat_determinism();
  test_sprintf_surface();

  if (g_failures == 0) {
    emit_line("TEST:PASS:clib_tinycc_link_surface");
    return 0;
  }

  emit_line("TEST:FAIL:clib_tinycc_link_surface");
  return 1;
}
