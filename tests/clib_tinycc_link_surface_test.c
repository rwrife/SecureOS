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
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "clib/errno.h"
#include "clib/malloc.h"
#include "clib/runtime_compat.h"
#include "clib/stdlib.h"
#include "secureos_api.h"

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
  /* #766 link-slice additions: float conversions + abort. */
  double (*sym_strtod)(const char *, char **) = strtod;
  float (*sym_strtof)(const char *, char **) = strtof;
  long double (*sym_strtold)(const char *, char **) = strtold;
  long double (*sym_ldexpl)(long double, int) = ldexpl;
  void (*sym_abort)(void) = abort;

  int ok = sym_realloc && sym_free && sym_sprintf && sym_exit && sym_time &&
           sym_localtime && sym_getcwd && sym_getenv && sym_realpath &&
           sym_dlopen && sym_dlsym && sym_strtod && sym_strtof &&
           sym_strtold && sym_ldexpl && sym_abort;
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

/*
 * Host-strong definition of the (weak-declared) process-exit bridge so
 * abort()'s termination path is observable: exit(134) -> os_process_exit
 * -> _exit(). Kernel contract is "never returns", mirrored here with
 * _exit; only the forked abort-probe child depends on the termination.
 */
os_status_t os_process_exit(int status) {
  _exit((unsigned char)status);
  return OS_STATUS_OK;
}

static void test_abort_through_bridge(void) {
  pid_t pid = fork();
  if (pid == 0) {
    abort();
    _exit(99); /* unreachable if abort terminates through the bridge */
  }
  if (pid > 0) {
    int status = 0;
    record_check(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                     WEXITSTATUS(status) == 134,
                 "abort_exits_134_through_bridge");
  } else {
    record_check(0, "abort_exits_134_through_bridge");
  }
}

/* strtod family pinned by clib/stdlib.h (issue #766 TinyCC float-literal
 * folding): deterministic digit-accumulation, exact on short constants. */
static void test_float_conversion_surface(void) {
  char *end = (char *)1;

  record_check(strtod("1.25", &end) == 1.25 && end != (char *)1 &&
                   *end == '\0',
               "strtod_decimal_exact");

  end = (char *)1;
  record_check(strtod(" -12.5e2x", &end) == -1250.0 && *end == 'x',
               "strtod_sign_exponent_endptr");

  end = (char *)1;
  record_check(strtof("3.5", &end) == 3.5f && *end == '\0',
               "strtof_basic");

  end = (char *)1;
  record_check(strtold("0x1.8p1", &end) == 3.0L && *end == '\0',
               "strtold_hexfloat");

  end = (char *)1;
  strtod("zzz", &end);
  record_check(end == (char *)"zzz" + 0 || end != (char *)1,
               "strtod_endptr_always_set");

  record_check(__builtin_isinf(strtod("inf", NULL)), "strtod_inf");
  record_check(__builtin_isnan(strtod("nan", NULL)), "strtod_nan");

  record_check(ldexpl(1.5L, 3) == 12.0L && ldexpl(3.0L, -1) == 1.5L &&
                   ldexpl(0.0L, 42) == 0.0L,
               "ldexpl_scales");
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
  test_float_conversion_surface();
  test_sprintf_surface();
  test_abort_through_bridge();

  if (g_failures == 0) {
    emit_line("TEST:PASS:clib_tinycc_link_surface");
    return 0;
  }

  emit_line("TEST:FAIL:clib_tinycc_link_surface");
  return 1;
}
