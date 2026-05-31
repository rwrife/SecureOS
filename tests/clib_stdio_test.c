/*
 * tests/clib_stdio_test.c
 *
 * Host unit test for the freestanding <stdio.h> nucleus shipped by
 * user/libs/clib (issue #447 / #407 — M7-TOOLCHAIN-004 slice 8,
 * plan plans/2026-05-28-in-os-toolchain-self-hosting.md P3).
 *
 * Strategy: register a recorder backend (in-memory file table + a
 * console buffer) and drive the entire stdio surface through it.
 * The clib FILE pool, format walker, and chunked console emitter
 * are exercised without any kernel / syscall dependency.
 *
 * Sub-markers (each round-trips via TEST:PASS:clib_stdio:...):
 *   - printf_basic_format            : %s %d round-trip exact bytes
 *   - printf_full_spec_set           : %u %x %p %c %% %ld %lu, width, zero-pad
 *   - fopen_fwrite_fread_round_trip  : "/tmp/foo" 64-byte payload
 *   - large_payload_round_trip       : >4 KiB payload (4097 bytes)
 *   - stderr_routes_to_console       : fprintf(stderr,...) hits console sink
 *   - fopen_invalid_mode_returns_null
 *   - defensive_no_backend           : no backend → fopen("r") returns NULL
 *   - shutdown_resets_pool           : after shutdown, full pool available
 *   - symbol_set_pinned              : drift guard
 *
 * Roll-up: TEST:PASS:clib_stdio (only on zero TEST:FAIL: lines).
 *
 * Compiled via build/scripts/test_clib_stdio.sh.
 */

/* We deliberately avoid <stdio.h> here: the clib FILE / fopen / printf
 * symbols collide with the host libc names. The harness writes its
 * own TEST:* markers via POSIX write(2) on fd 1 / fd 2, which has no
 * libc-stdio dependency. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>

/* The freestanding stdio header conflicts with the host <stdio.h> on
 * symbols like FILE / stdout / stderr / printf. We deliberately avoid
 * including the clib header here and instead declare every symbol we
 * call by hand — this gives us per-call type discipline AND doubles
 * as a drift guard (if any signature changes the link fails). */

typedef enum {
  CLIB_STDIO_OK     = 0,
  CLIB_STDIO_DENIED = 1,
  CLIB_STDIO_ERROR  = 2
} clib_stdio_status_t;

typedef struct clib_stdio_backend {
  clib_stdio_status_t (*read_file)(const char *path, char *buf, size_t *io_size, void *ctx);
  clib_stdio_status_t (*write_file)(const char *path, const char *content, size_t size, int append, void *ctx);
  clib_stdio_status_t (*console_write)(const char *message, void *ctx);
  void *ctx;
} clib_stdio_backend_t;

struct clib_FILE;
typedef struct clib_FILE clib_FILE_t;

extern clib_FILE_t *stdin;
extern clib_FILE_t *stdout;
extern clib_FILE_t *stderr;

void clib_stdio_init(const clib_stdio_backend_t *backend);
void clib_stdio_shutdown(void);

clib_FILE_t *fopen(const char *path, const char *mode) __asm__("fopen");
int          fclose(clib_FILE_t *fp) __asm__("fclose");
size_t       fread (void *buf, size_t size, size_t nmemb, clib_FILE_t *fp) __asm__("fread");
size_t       fwrite(const void *buf, size_t size, size_t nmemb, clib_FILE_t *fp) __asm__("fwrite");
int          fflush(clib_FILE_t *fp) __asm__("fflush");
int          fputs (const char *s, clib_FILE_t *fp) __asm__("fputs");
int          fputc (int c, clib_FILE_t *fp) __asm__("fputc");
int          fprintf(clib_FILE_t *fp, const char *fmt, ...) __asm__("fprintf");
int          printf (const char *fmt, ...) __asm__("printf");
int          feof  (clib_FILE_t *fp) __asm__("feof");
int          ferror(clib_FILE_t *fp) __asm__("ferror");

/* Harness output: minimal printf-on-write(2) so we never touch host
 * libc FILE machinery (which would alias-conflict with the clib
 * symbols we are linking in). Format support is just %s %d %zu, which
 * is everything the harness itself emits. */
static void htoa_signed(long long v, char *out, size_t *plen) {
  char tmp[32]; size_t n = 0; int neg = 0;
  unsigned long long mag;
  if (v < 0) { neg = 1; mag = (unsigned long long)(-(v + 1)) + 1ULL; }
  else mag = (unsigned long long)v;
  if (mag == 0) tmp[n++] = '0';
  while (mag) { tmp[n++] = (char)('0' + (mag % 10)); mag /= 10; }
  size_t o = 0;
  if (neg) out[o++] = '-';
  for (size_t i = 0; i < n; ++i) out[o++] = tmp[n - 1 - i];
  *plen = o;
}
static void hwrite(int fd, const char *buf, size_t n) {
  ssize_t r; size_t off = 0;
  while (off < n) { r = write(fd, buf + off, n - off); if (r <= 0) break; off += (size_t)r; }
}
static void hvprintf(int fd, const char *fmt, va_list ap) {
  char buf[1024]; size_t o = 0;
  for (const char *p = fmt; *p && o + 64 < sizeof buf; ++p) {
    if (*p != '%') { buf[o++] = *p; continue; }
    ++p;
    if (*p == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      while (*s && o + 1 < sizeof buf) buf[o++] = *s++;
    } else if (*p == 'd') {
      long long v = va_arg(ap, int);
      char num[32]; size_t nl;
      htoa_signed(v, num, &nl);
      for (size_t i = 0; i < nl && o + 1 < sizeof buf; ++i) buf[o++] = num[i];
    } else if (*p == 'z' && p[1] == 'u') {
      ++p;
      unsigned long long v = va_arg(ap, size_t);
      char num[32]; size_t nl;
      htoa_signed((long long)v, num, &nl);
      for (size_t i = 0; i < nl && o + 1 < sizeof buf; ++i) buf[o++] = num[i];
    } else {
      buf[o++] = '%'; if (*p && o + 1 < sizeof buf) buf[o++] = *p;
    }
  }
  hwrite(fd, buf, o);
}
static void hprintf_out(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); hvprintf(1, fmt, ap); va_end(ap);
}
static void hprintf_err(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); hvprintf(2, fmt, ap); va_end(ap);
}
#define hprintf(...) hprintf_out(__VA_ARGS__)

/* --- recorder backend ----------------------------------------------------- */

#define REC_FILES_MAX 8
#define REC_BUF_CAP   65536
#define REC_CON_CAP   65536

typedef struct {
  char           path[256];
  unsigned char  buf[REC_BUF_CAP];
  size_t         len;
  int            in_use;
} rec_file_t;

static rec_file_t   g_rec_files[REC_FILES_MAX];
static unsigned char g_rec_console[REC_CON_CAP];
static size_t        g_rec_console_len;
static int           g_rec_read_calls;
static int           g_rec_write_calls;
static int           g_rec_console_calls;

static void rec_reset(void) {
  memset(g_rec_files, 0, sizeof g_rec_files);
  memset(g_rec_console, 0, sizeof g_rec_console);
  g_rec_console_len   = 0;
  g_rec_read_calls    = 0;
  g_rec_write_calls   = 0;
  g_rec_console_calls = 0;
}

static rec_file_t *rec_lookup(const char *path) {
  for (int i = 0; i < REC_FILES_MAX; ++i)
    if (g_rec_files[i].in_use && strcmp(g_rec_files[i].path, path) == 0)
      return &g_rec_files[i];
  return NULL;
}

static rec_file_t *rec_create(const char *path) {
  for (int i = 0; i < REC_FILES_MAX; ++i)
    if (!g_rec_files[i].in_use) {
      g_rec_files[i].in_use = 1;
      g_rec_files[i].len    = 0;
      strncpy(g_rec_files[i].path, path, sizeof g_rec_files[i].path - 1);
      g_rec_files[i].path[sizeof g_rec_files[i].path - 1] = '\0';
      return &g_rec_files[i];
    }
  return NULL;
}

static clib_stdio_status_t rec_read(const char *path, char *buf, size_t *io_size, void *ctx) {
  (void)ctx;
  g_rec_read_calls++;
  rec_file_t *f = rec_lookup(path);
  if (!f) return CLIB_STDIO_ERROR;
  size_t take = (f->len < *io_size) ? f->len : *io_size;
  memcpy(buf, f->buf, take);
  *io_size = take;
  return CLIB_STDIO_OK;
}

static clib_stdio_status_t rec_write(const char *path, const char *content, size_t size, int append, void *ctx) {
  (void)ctx;
  g_rec_write_calls++;
  rec_file_t *f = rec_lookup(path);
  if (!f) f = rec_create(path);
  if (!f) return CLIB_STDIO_ERROR;
  size_t start = append ? f->len : 0;
  if (!append) f->len = 0;
  if (start + size > REC_BUF_CAP) return CLIB_STDIO_ERROR;
  memcpy(f->buf + start, content, size);
  if (start + size > f->len) f->len = start + size;
  return CLIB_STDIO_OK;
}

static clib_stdio_status_t rec_console(const char *message, void *ctx) {
  (void)ctx;
  g_rec_console_calls++;
  size_t n = strlen(message);
  if (g_rec_console_len + n > REC_CON_CAP) return CLIB_STDIO_ERROR;
  memcpy(g_rec_console + g_rec_console_len, message, n);
  g_rec_console_len += n;
  return CLIB_STDIO_OK;
}

/* --- assertion helper ----------------------------------------------------- */

static int g_fail = 0;

#define CHECK(cond, name)                                                   \
  do {                                                                      \
    if (!(cond)) {                                                          \
      hprintf_err("TEST:FAIL:clib_stdio:%s (line %d)\n",                   \
                  (name), __LINE__);                                        \
      g_fail = 1;                                                           \
    }                                                                       \
  } while (0)

#define PASS(name) hprintf("TEST:PASS:clib_stdio:%s\n", (name))

/* --- tests ---------------------------------------------------------------- */

static const clib_stdio_backend_t k_full_backend = {
  rec_read, rec_write, rec_console, NULL
};

static void install_full_backend(void) {
  rec_reset();
  clib_stdio_shutdown();
  clib_stdio_init(&k_full_backend);
}

static void test_printf_basic_format(void) {
  install_full_backend();
  int n = printf("hello %s %d\n", "x", 42);
  int ok = (n == 11)
        && (g_rec_console_len == 11)
        && (memcmp(g_rec_console, "hello x 42\n", 11) == 0);
  CHECK(ok, "printf_basic_format");
  if (ok) PASS("printf_basic_format");
}

static void test_printf_full_spec_set(void) {
  install_full_backend();
  /* Cover %u %x %p %c %% %ld %lu and width/zero-pad knobs. */
  void *ptr = (void *)(uintptr_t)0xdeadbeef;
  int n = printf("%u|%x|%p|%c|%%|%ld|%lu|%08d|%-5s|", 7u, 0xabcu, ptr, 'Z',
                 -123L, 9999UL, 42, "hi");
  /* Expected: "7|abc|0xdeadbeef|Z|%|-123|9999|00000042|hi   |" */
  const char *expect = "7|abc|0xdeadbeef|Z|%|-123|9999|00000042|hi   |";
  size_t exp_len = strlen(expect);
  int ok = ((size_t)n == exp_len)
        && (g_rec_console_len == exp_len)
        && (memcmp(g_rec_console, expect, exp_len) == 0);
  if (!ok) {
    /* Print captured console + expected for debugging via fd 2. */
    hprintf_err("DEBUG full_spec got_len=%zu want_len=%zu\n",
                g_rec_console_len, exp_len);
    hwrite(2, (const char *)g_rec_console, g_rec_console_len);
    hwrite(2, "\n", 1);
    hwrite(2, expect, exp_len);
    hwrite(2, "\n", 1);
  }
  CHECK(ok, "printf_full_spec_set");
  if (ok) PASS("printf_full_spec_set");
}

static void test_fopen_fwrite_fread_round_trip(void) {
  install_full_backend();
  clib_FILE_t *w = fopen("/tmp/foo", "w");
  CHECK(w != NULL, "fopen_w_returns_handle");
  const char payload[] = "round-trip-small-payload-0123456789ABCDEFGHIJabcdefghij[end]\n";
  size_t plen = sizeof payload - 1;
  size_t wrote = fwrite(payload, 1, plen, w);
  CHECK(wrote == plen, "fwrite_returns_full_count");
  CHECK(fclose(w) == 0, "fclose_w_ok");

  clib_FILE_t *r = fopen("/tmp/foo", "r");
  CHECK(r != NULL, "fopen_r_returns_handle");
  char back[256] = {0};
  size_t got = fread(back, 1, sizeof back, r);
  CHECK(got == plen, "fread_returns_full_count");
  int ok = (memcmp(back, payload, plen) == 0);
  CHECK(ok, "fread_bytes_match");
  CHECK(fclose(r) == 0, "fclose_r_ok");
  if (ok && got == plen) PASS("fopen_fwrite_fread_round_trip");
}

static void test_large_payload_round_trip(void) {
  install_full_backend();
  /* 4097 bytes: deliberately above 4 KiB per #447 acceptance. */
  enum { N = 4097 };
  static unsigned char src[N];
  for (size_t i = 0; i < N; ++i) src[i] = (unsigned char)(i * 31u + 7u);

  clib_FILE_t *w = fopen("/tmp/big.bin", "w");
  CHECK(w != NULL, "large_fopen_w");
  size_t wrote = fwrite(src, 1, N, w);
  CHECK(wrote == N, "large_fwrite_full");
  CHECK(fclose(w) == 0, "large_fclose_w");

  /* Backend file recorded? */
  rec_file_t *rf = rec_lookup("/tmp/big.bin");
  CHECK(rf != NULL && rf->len == N && memcmp(rf->buf, src, N) == 0,
        "large_backend_state_matches");

  clib_FILE_t *r = fopen("/tmp/big.bin", "r");
  CHECK(r != NULL, "large_fopen_r");
  static unsigned char back[N];
  memset(back, 0, sizeof back);
  size_t got = fread(back, 1, N, r);
  CHECK(got == N, "large_fread_full");
  int ok = (memcmp(back, src, N) == 0);
  CHECK(ok, "large_bytes_match");
  CHECK(fclose(r) == 0, "large_fclose_r");
  if (ok && got == N) PASS("large_payload_round_trip");
}

static void test_stderr_routes_to_console(void) {
  /* Backend with NULL write_file — proves stderr does NOT hit the
   * file sink (which would crash on the NULL pointer) and instead
   * goes through console_write. */
  clib_stdio_backend_t console_only = {
    NULL, NULL, rec_console, NULL
  };
  rec_reset();
  clib_stdio_shutdown();
  clib_stdio_init(&console_only);
  int n = fprintf(stderr, "err %d %s\n", 7, "msg");
  int ok = (n == 10)
        && (g_rec_console_len == 10)
        && (memcmp(g_rec_console, "err 7 msg\n", 10) == 0)
        && (g_rec_write_calls == 0);
  CHECK(ok, "stderr_routes_to_console");
  if (ok) PASS("stderr_routes_to_console");
}

static void test_fopen_invalid_mode_returns_null(void) {
  install_full_backend();
  clib_FILE_t *f = fopen("/tmp/foo", "rb+");
  int ok = (f == NULL);
  CHECK(ok, "fopen_invalid_mode_returns_null");
  if (ok) PASS("fopen_invalid_mode_returns_null");
}

static void test_defensive_no_backend(void) {
  rec_reset();
  clib_stdio_shutdown();
  clib_stdio_init(NULL); /* explicit clear */
  clib_FILE_t *f = fopen("/tmp/x", "r");
  int ok = (f == NULL);
  CHECK(ok, "defensive_no_backend_read");
  /* fprintf to stdout with no backend returns -1 (console sink unset). */
  int n = printf("blocked");
  CHECK(n == -1, "defensive_no_backend_console");
  if (ok && n == -1) PASS("defensive_no_backend");
}

static void test_shutdown_resets_pool(void) {
  install_full_backend();
  /* Open the pool to exhaustion: 8 writers. */
  clib_FILE_t *handles[8];
  int opened = 0;
  for (int i = 0; i < 8; ++i) {
    char path[32];
    path[0] = '/'; path[1] = 't'; path[2] = 'm'; path[3] = 'p';
    path[4] = '/'; path[5] = 'p'; path[6] = (char)('0' + i); path[7] = '\0';
    handles[i] = fopen(path, "w");
    if (handles[i]) opened++;
  }
  CHECK(opened == 8, "shutdown_pool_fills");
  /* A 9th open should fail (pool exhausted). */
  CHECK(fopen("/tmp/overflow", "w") == NULL, "shutdown_pool_overflow_rejected");
  /* shutdown wipes the pool; re-init lets us open 8 again. */
  clib_stdio_shutdown();
  clib_stdio_init(&k_full_backend);
  int reopened = 0;
  for (int i = 0; i < 8; ++i) {
    char path[32];
    path[0] = '/'; path[1] = 't'; path[2] = 'm'; path[3] = 'p';
    path[4] = '/'; path[5] = 'q'; path[6] = (char)('0' + i); path[7] = '\0';
    if (fopen(path, "w") != NULL) reopened++;
  }
  CHECK(reopened == 8, "shutdown_reset_allows_full_reuse");
  if (opened == 8 && reopened == 8) PASS("shutdown_resets_pool");
}

static void test_symbol_set_pinned(void) {
  /* Drift guard — take the address of every public symbol through a
   * function pointer table. If any signature changes or any symbol is
   * silently dropped, the link fails. (Same shape as the str/mem,
   * ctype, qsort, stdlib, errno, stdarg, bsearch slices.) */
  void *pins[] = {
    (void *)(uintptr_t)&clib_stdio_init,
    (void *)(uintptr_t)&clib_stdio_shutdown,
    (void *)(uintptr_t)&fopen,
    (void *)(uintptr_t)&fclose,
    (void *)(uintptr_t)&fread,
    (void *)(uintptr_t)&fwrite,
    (void *)(uintptr_t)&fflush,
    (void *)(uintptr_t)&fputs,
    (void *)(uintptr_t)&fputc,
    (void *)(uintptr_t)&fprintf,
    (void *)(uintptr_t)&printf,
    (void *)(uintptr_t)&feof,
    (void *)(uintptr_t)&ferror,
    (void *)(uintptr_t)&stdin,
    (void *)(uintptr_t)&stdout,
    (void *)(uintptr_t)&stderr,
  };
  int ok = 1;
  for (size_t i = 0; i < sizeof pins / sizeof pins[0]; ++i)
    if (pins[i] == NULL) ok = 0;
  CHECK(ok, "symbol_set_pinned");
  if (ok) PASS("symbol_set_pinned");
}

int main(void) {
  /* Each test installs its own backend / reset state. */
  test_printf_basic_format();
  test_printf_full_spec_set();
  test_fopen_fwrite_fread_round_trip();
  test_large_payload_round_trip();
  test_stderr_routes_to_console();
  test_fopen_invalid_mode_returns_null();
  test_defensive_no_backend();
  test_shutdown_resets_pool();
  test_symbol_set_pinned();

  if (!g_fail) hprintf("TEST:PASS:clib_stdio\n");
  return g_fail ? 1 : 0;
}
