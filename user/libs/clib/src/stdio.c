/**
 * @file src/stdio.c
 * @brief Freestanding `<stdio.h>` nucleus implementation
 *        (issue #447 / #407 — M7-TOOLCHAIN-004 slice 8, plan
 *         `plans/2026-05-28-in-os-toolchain-self-hosting.md` P3).
 *
 * See `include/clib/stdio.h` for surface, backend contract, and
 * scope rationale. This translation unit:
 *
 *   - keeps a static pool of FILE slots (no heap dependency — same
 *     deterministic-state policy str/mem/ctype/qsort/stdlib/errno/
 *     stdarg slices follow);
 *   - wires `stdout` / `stderr` to the backend's `console_write`
 *     sink with chunked NUL-terminated flushes;
 *   - wires `fopen("r"|"w"|"a")` to `read_file` / `write_file`
 *     with the truncate-vs-append semantics documented in the
 *     header;
 *   - ships a minimal `vfprintf` covering the TinyCC / `cc` format
 *     spec set (`%s %d %u %x %p %c %% %ld %lu`, optional width,
 *     `%0Nd` zero-pad, `%-Nd` left-justify). No floats, no `*`
 *     width, no positional args.
 *
 * Containment: no libc, no kernel includes, no syscalls. Pure C11
 * over `<stddef.h>` and our local headers.
 */

#include "../include/clib/stdio.h"

#include <stddef.h>
#include <stdint.h>

/* ---- Backend registration ------------------------------------------------ */

static clib_stdio_backend_t g_backend; /* zero-init: all fns NULL */
static int                  g_backend_set = 0;

void clib_stdio_init(const clib_stdio_backend_t *backend) {
  if (backend == 0) {
    /* explicit clear */
    g_backend.read_file     = 0;
    g_backend.write_file    = 0;
    g_backend.console_write = 0;
    g_backend.ctx           = 0;
    g_backend_set           = 0;
    return;
  }
  g_backend     = *backend;
  g_backend_set = 1;
}

/* ---- FILE pool ----------------------------------------------------------- */

#define CLIB_STDIO_POOL_SIZE     8
#define CLIB_STDIO_FILE_BUF_CAP  8192 /* >4 KiB round-trip per #447 */
#define CLIB_STDIO_PATH_MAX      256

typedef enum {
  CLIB_FK_STD_OUT = 1, /* console sink, NUL-terminated chunked flush */
  CLIB_FK_STD_ERR = 2, /* console sink (same backend, different label) */
  CLIB_FK_FILE_R  = 3, /* read snapshot: buffer is the file contents */
  CLIB_FK_FILE_W  = 4, /* write buffer: flushed via write_file(append=0) */
  CLIB_FK_FILE_A  = 5  /* write buffer: flushed via write_file(append=1) */
} clib_file_kind_t;

struct clib_FILE {
  int              in_use;
  clib_file_kind_t kind;
  char             path[CLIB_STDIO_PATH_MAX];
  /* Buffer + cursor + length.
   *  - READ:  length = bytes read; cursor = next read offset; len = total
   *  - WRITE: length = bytes accumulated; cursor unused; len = bytes
   *           still to flush. After a successful flush we mark
   *           `flushed_once` so subsequent flushes use append mode (we
   *           never overwrite earlier flushed bytes).
   */
  unsigned char    buf[CLIB_STDIO_FILE_BUF_CAP];
  size_t           len;
  size_t           cursor;
  int              flushed_once;
  int              eof_flag;
  int              err_flag;
};

static struct clib_FILE g_pool[CLIB_STDIO_POOL_SIZE];

/* Standard streams: three permanent FILE slots living outside the
 * pool (so `fclose(stdout)` cannot consume a pool slot, and a
 * shutdown / re-init cycle does not invalidate `stdout` / `stderr`
 * pointers held by callers). */
static struct clib_FILE g_stdin  = { .in_use = 1, .kind = CLIB_FK_FILE_R };
static struct clib_FILE g_stdout = { .in_use = 1, .kind = CLIB_FK_STD_OUT };
static struct clib_FILE g_stderr = { .in_use = 1, .kind = CLIB_FK_STD_ERR };

FILE *stdin  = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

void clib_stdio_shutdown(void) {
  for (size_t i = 0; i < CLIB_STDIO_POOL_SIZE; ++i) {
    g_pool[i].in_use       = 0;
    g_pool[i].kind         = 0;
    g_pool[i].len          = 0;
    g_pool[i].cursor       = 0;
    g_pool[i].flushed_once = 0;
    g_pool[i].eof_flag     = 0;
    g_pool[i].err_flag     = 0;
    g_pool[i].path[0]      = '\0';
  }
  /* Reset console streams' error flags but keep them addressable. */
  g_stdout.err_flag = 0;
  g_stderr.err_flag = 0;
  g_stdin.eof_flag  = 0;
  g_stdin.err_flag  = 0;
  /* Clear backend last so callers see the cleared state. */
  clib_stdio_init(0);
}

/* ---- Helpers ------------------------------------------------------------- */

static size_t clib_strlen(const char *s) {
  size_t n = 0;
  while (s && s[n] != '\0') ++n;
  return n;
}

static void clib_memcpy(void *dst, const void *src, size_t n) {
  unsigned char       *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

static int clib_streq(const char *a, const char *b) {
  if (a == 0 || b == 0) return 0;
  while (*a && *b && *a == *b) { ++a; ++b; }
  return *a == *b;
}

static int clib_strcpy_bounded(char *dst, size_t cap, const char *src) {
  if (cap == 0 || dst == 0 || src == 0) return -1;
  size_t i = 0;
  for (; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = src[i];
  dst[i] = '\0';
  return (src[i] == '\0') ? 0 : -1;
}

static struct clib_FILE *pool_alloc(void) {
  for (size_t i = 0; i < CLIB_STDIO_POOL_SIZE; ++i) {
    if (!g_pool[i].in_use) {
      struct clib_FILE *fp = &g_pool[i];
      fp->in_use       = 1;
      fp->kind         = 0;
      fp->len          = 0;
      fp->cursor       = 0;
      fp->flushed_once = 0;
      fp->eof_flag     = 0;
      fp->err_flag     = 0;
      fp->path[0]      = '\0';
      return fp;
    }
  }
  return 0;
}

/* Console emission: NUL-terminated chunked write of an arbitrary byte
 * range. The contract from the header (and from `os_console_write` on
 * target) is that the sink receives a NUL-terminated C string. For
 * `fwrite(stdout)` of non-string bytes we walk the buffer in chunks
 * of (CHUNK-1) bytes, NUL-terminating each chunk in a stack-resident
 * temp. This bounds the host-test recorder shim too — every captured
 * console message is guaranteed NUL-terminated. */
#define CLIB_STDIO_CONSOLE_CHUNK 256

static int console_emit(const unsigned char *bytes, size_t n) {
  if (!g_backend_set || g_backend.console_write == 0) return -1;
  char tmp[CLIB_STDIO_CONSOLE_CHUNK];
  size_t off = 0;
  while (off < n) {
    size_t take = n - off;
    if (take > CLIB_STDIO_CONSOLE_CHUNK - 1)
      take = CLIB_STDIO_CONSOLE_CHUNK - 1;
    clib_memcpy(tmp, bytes + off, take);
    tmp[take] = '\0';
    if (g_backend.console_write(tmp, g_backend.ctx) != CLIB_STDIO_OK)
      return -1;
    off += take;
  }
  return 0;
}

/* ---- fopen / fclose ------------------------------------------------------ */

FILE *fopen(const char *path, const char *mode) {
  if (path == 0 || mode == 0) return 0;
  struct clib_FILE *fp = pool_alloc();
  if (fp == 0) return 0;
  if (clib_strcpy_bounded(fp->path, sizeof fp->path, path) != 0) {
    fp->in_use = 0;
    return 0;
  }
  if (clib_streq(mode, "r")) {
    fp->kind = CLIB_FK_FILE_R;
    /* Snapshot file contents at open time (single backend call). */
    size_t cap = sizeof fp->buf;
    if (!g_backend_set || g_backend.read_file == 0) {
      fp->in_use = 0;
      return 0;
    }
    if (g_backend.read_file(path, (char *)fp->buf, &cap, g_backend.ctx)
        != CLIB_STDIO_OK) {
      fp->in_use = 0;
      return 0;
    }
    fp->len    = cap;
    fp->cursor = 0;
    return fp;
  }
  if (clib_streq(mode, "w")) {
    fp->kind = CLIB_FK_FILE_W;
    return fp;
  }
  if (clib_streq(mode, "a")) {
    fp->kind = CLIB_FK_FILE_A;
    return fp;
  }
  fp->in_use = 0;
  return 0;
}

int fclose(FILE *fp) {
  if (fp == 0) return EOF;
  int rc = 0;
  if (fp->kind == CLIB_FK_FILE_W || fp->kind == CLIB_FK_FILE_A) {
    if (fflush(fp) != 0) rc = EOF;
  }
  /* Refuse to free the static standard-stream slots. */
  if (fp == &g_stdin || fp == &g_stdout || fp == &g_stderr) return rc;
  fp->in_use = 0;
  return rc;
}

/* ---- fread / fwrite / fflush -------------------------------------------- */

size_t fread(void *buf, size_t size, size_t nmemb, FILE *fp) {
  if (buf == 0 || fp == 0 || size == 0 || nmemb == 0) return 0;
  if (fp->kind != CLIB_FK_FILE_R) {
    fp->err_flag = 1;
    return 0;
  }
  size_t want = size * nmemb;
  size_t avail = (fp->cursor <= fp->len) ? (fp->len - fp->cursor) : 0;
  size_t take = (want < avail) ? want : avail;
  clib_memcpy(buf, fp->buf + fp->cursor, take);
  fp->cursor += take;
  if (take < want) fp->eof_flag = 1;
  return take / size;
}

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *fp) {
  if (buf == 0 || fp == 0 || size == 0 || nmemb == 0) return 0;
  size_t want = size * nmemb;
  if (fp->kind == CLIB_FK_STD_OUT || fp->kind == CLIB_FK_STD_ERR) {
    if (console_emit((const unsigned char *)buf, want) != 0) {
      fp->err_flag = 1;
      return 0;
    }
    return nmemb;
  }
  if (fp->kind == CLIB_FK_FILE_W || fp->kind == CLIB_FK_FILE_A) {
    /* Accumulate into the static per-FILE buffer; refuse to overflow. */
    if (fp->len + want > sizeof fp->buf) {
      fp->err_flag = 1;
      /* Partial-fit: write what we can, return short count. */
      size_t fit = sizeof fp->buf - fp->len;
      size_t fit_units = fit / size;
      size_t fit_bytes = fit_units * size;
      clib_memcpy(fp->buf + fp->len, buf, fit_bytes);
      fp->len += fit_bytes;
      return fit_units;
    }
    clib_memcpy(fp->buf + fp->len, buf, want);
    fp->len += want;
    return nmemb;
  }
  fp->err_flag = 1;
  return 0;
}

int fflush(FILE *fp) {
  if (fp == 0) return 0; /* libc convention: fflush(NULL) flushes all */
  if (fp->kind == CLIB_FK_STD_OUT || fp->kind == CLIB_FK_STD_ERR) {
    /* Console writes are unbuffered — no work to do. */
    return 0;
  }
  if (fp->kind == CLIB_FK_FILE_W || fp->kind == CLIB_FK_FILE_A) {
    if (fp->len == 0) return 0;
    if (!g_backend_set || g_backend.write_file == 0) {
      fp->err_flag = 1;
      return EOF;
    }
    int append = (fp->kind == CLIB_FK_FILE_A) ? 1 : 0;
    if (fp->flushed_once) append = 1; /* never re-truncate after first flush */
    if (g_backend.write_file(fp->path,
                             (const char *)fp->buf,
                             fp->len,
                             append,
                             g_backend.ctx) != CLIB_STDIO_OK) {
      fp->err_flag = 1;
      return EOF;
    }
    fp->flushed_once = 1;
    fp->len          = 0; /* buffer drained */
    return 0;
  }
  return 0;
}

/* ---- fputs / fputc ------------------------------------------------------- */

int fputs(const char *s, FILE *fp) {
  if (s == 0 || fp == 0) return EOF;
  size_t n = clib_strlen(s);
  size_t wrote = fwrite(s, 1, n, fp);
  return (wrote == n) ? (int)n : EOF;
}

int fputc(int c, FILE *fp) {
  unsigned char b = (unsigned char)c;
  if (fwrite(&b, 1, 1, fp) != 1) return EOF;
  return (int)b;
}

/* ---- vfprintf: minimal format walker ------------------------------------ */
/*
 * Supported specifiers:
 *   %s         — NUL-terminated string; NULL prints as "(null)"
 *   %d %i      — int (decimal)
 *   %u         — unsigned int (decimal)
 *   %x         — unsigned int (lowercase hex)
 *   %p         — void* (lowercase hex, "0x" prefix)
 *   %c         — int promoted byte
 *   %%         — literal '%'
 *   %ld %li    — long
 *   %lu        — unsigned long
 *   %lx        — unsigned long (hex)
 *   width:     optional non-zero decimal, e.g. %8d
 *   pad:       leading '0' for zero-pad, '-' for left-justify
 *
 * Not supported (and we silently echo the literal spec to make the
 * regression visible in the recorder shim):
 *   %f %g %e   — no floats
 *   *          — width-from-arg
 *   %1$s       — positional args
 */

typedef struct {
  unsigned char *out;
  size_t         cap;
  size_t         used;
} fmt_sink_t;

static void sink_put(fmt_sink_t *sk, unsigned char b) {
  if (sk->used < sk->cap) sk->out[sk->used] = b;
  sk->used++;
}

static void sink_pad(fmt_sink_t *sk, unsigned char pad, size_t n) {
  for (size_t i = 0; i < n; ++i) sink_put(sk, pad);
}

/* Render an unsigned 64-bit value in base 10 or 16 (lowercase). */
static size_t render_uint(unsigned char *tmp, size_t cap,
                          unsigned long long v, unsigned base) {
  if (cap == 0) return 0;
  static const char digits[] = "0123456789abcdef";
  unsigned char buf[32];
  size_t        n = 0;
  if (v == 0) { buf[n++] = '0'; }
  while (v > 0 && n < sizeof buf) {
    buf[n++] = (unsigned char)digits[v % base];
    v /= base;
  }
  if (n > cap) n = cap;
  for (size_t i = 0; i < n; ++i) tmp[i] = buf[n - 1 - i];
  return n;
}

static void emit_int_field(fmt_sink_t *sk,
                           unsigned long long mag, int negative,
                           unsigned base, int width, int zero_pad,
                           int left_justify, int force_hex_prefix) {
  unsigned char tmp[32];
  size_t        body = render_uint(tmp, sizeof tmp, mag, base);
  size_t        sign_len = negative ? 1 : 0;
  size_t        prefix_len = force_hex_prefix ? 2 : 0;
  size_t        total = body + sign_len + prefix_len;
  size_t        pad = (width > 0 && (size_t)width > total)
                          ? (size_t)width - total
                          : 0;
  if (!left_justify && !zero_pad) sink_pad(sk, ' ', pad);
  if (negative) sink_put(sk, '-');
  if (force_hex_prefix) { sink_put(sk, '0'); sink_put(sk, 'x'); }
  if (!left_justify && zero_pad)  sink_pad(sk, '0', pad);
  for (size_t i = 0; i < body; ++i) sink_put(sk, tmp[i]);
  if (left_justify) sink_pad(sk, ' ', pad);
}

static int do_vfprintf_into(fmt_sink_t *sk, const char *fmt, va_list ap) {
  const char *p = fmt;
  while (*p) {
    if (*p != '%') {
      sink_put(sk, (unsigned char)*p++);
      continue;
    }
    const char *spec_start = p;
    ++p; /* past '%' */
    int left_justify = 0;
    int zero_pad     = 0;
    while (*p == '-' || *p == '0') {
      if (*p == '-') left_justify = 1;
      if (*p == '0') zero_pad     = 1;
      ++p;
    }
    int width = 0;
    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      ++p;
    }
    int is_long = 0;
    if (*p == 'l') { is_long = 1; ++p; }
    char conv = *p;
    if (conv == '\0') {
      /* trailing '%' — emit literally for visibility */
      for (const char *q = spec_start; q < p; ++q)
        sink_put(sk, (unsigned char)*q);
      break;
    }
    ++p;
    switch (conv) {
      case '%':
        sink_put(sk, '%');
        break;
      case 'c': {
        int c = va_arg(ap, int);
        if (width > 1 && !left_justify) sink_pad(sk, ' ', (size_t)(width - 1));
        sink_put(sk, (unsigned char)c);
        if (width > 1 && left_justify)  sink_pad(sk, ' ', (size_t)(width - 1));
        break;
      }
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (s == 0) s = "(null)";
        size_t n = clib_strlen(s);
        size_t pad = (width > 0 && (size_t)width > n) ? (size_t)width - n : 0;
        if (!left_justify) sink_pad(sk, ' ', pad);
        for (size_t i = 0; i < n; ++i) sink_put(sk, (unsigned char)s[i]);
        if (left_justify)  sink_pad(sk, ' ', pad);
        break;
      }
      case 'd':
      case 'i': {
        long long v = is_long ? (long long)va_arg(ap, long)
                              : (long long)va_arg(ap, int);
        int neg = (v < 0);
        unsigned long long mag = neg
            ? (unsigned long long)(-(v + 1)) + 1ULL
            : (unsigned long long)v;
        emit_int_field(sk, mag, neg, 10, width, zero_pad, left_justify, 0);
        break;
      }
      case 'u': {
        unsigned long long v = is_long
            ? (unsigned long long)va_arg(ap, unsigned long)
            : (unsigned long long)va_arg(ap, unsigned int);
        emit_int_field(sk, v, 0, 10, width, zero_pad, left_justify, 0);
        break;
      }
      case 'x': {
        unsigned long long v = is_long
            ? (unsigned long long)va_arg(ap, unsigned long)
            : (unsigned long long)va_arg(ap, unsigned int);
        emit_int_field(sk, v, 0, 16, width, zero_pad, left_justify, 0);
        break;
      }
      case 'p': {
        void              *vp = va_arg(ap, void *);
        unsigned long long v  = (unsigned long long)(uintptr_t)vp;
        emit_int_field(sk, v, 0, 16, width, zero_pad, left_justify, 1);
        break;
      }
      default:
        /* Unsupported specifier: echo the literal sequence so the
         * regression is visible to the recorder shim. */
        for (const char *q = spec_start; q < p; ++q)
          sink_put(sk, (unsigned char)*q);
        break;
    }
  }
  return (int)sk->used;
}

#define CLIB_STDIO_FMT_STACK_BUF 1024

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
  if (fp == 0 || fmt == 0) return -1;
  unsigned char stackbuf[CLIB_STDIO_FMT_STACK_BUF];
  fmt_sink_t    sk = { stackbuf, sizeof stackbuf, 0 };
  int           full_len = do_vfprintf_into(&sk, fmt, ap);
  size_t        emit_len = (sk.used < sk.cap) ? sk.used : sk.cap;
  /* If the output overran the stack buffer we still emit the
   * truncation so the recorder sees the first 1 KiB intact; full
   * length is returned so callers know they hit the cap. */
  if (fp->kind == CLIB_FK_STD_OUT || fp->kind == CLIB_FK_STD_ERR) {
    if (console_emit(stackbuf, emit_len) != 0) return -1;
  } else {
    size_t n = fwrite(stackbuf, 1, emit_len, fp);
    if (n != emit_len) return -1;
  }
  return full_len;
}

int fprintf(FILE *fp, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(fp, fmt, ap);
  va_end(ap);
  return r;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}

/* ---- snprintf / vsnprintf -----------------------------------------------
 *
 * Pure-memory format renderers — no FILE, no backend, no syscalls.
 * Reuses the exact same `do_vfprintf_into` walker as `vfprintf` so
 * the spec set stays in lock-step (one place to add features, one
 * place to add bugs).
 *
 * C11 §7.21.6.5 (snprintf):
 *   - if `size > 0`, writes up to `size - 1` formatted bytes to
 *     `buf` followed by a terminating `'\0'`;
 *   - if `size == 0`, `buf` may be NULL and no bytes are written;
 *   - returns the number of bytes the *full* formatted output
 *     would have required (NUL excluded), so callers can detect
 *     truncation via `ret >= size`.
 *
 * On a NULL `fmt` we return -1 (mirrors `vfprintf`).
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
  if (fmt == 0) return -1;
  /* If size == 0 we still need to compute the full length so the
   * caller can size a buffer. We point the walker at a 1-byte
   * scratch slot it will overwrite but never read; sink_put tracks
   * `used` independently of `cap`, so the returned length is the
   * full requested length. */
  unsigned char scratch;
  unsigned char *out;
  size_t         out_cap;
  if (size == 0 || buf == 0) {
    out     = &scratch;
    out_cap = 0;
  } else {
    out     = (unsigned char *)buf;
    /* Reserve one slot for the trailing NUL. */
    out_cap = size - 1;
  }
  fmt_sink_t sk = { out, out_cap, 0 };
  int        full_len = do_vfprintf_into(&sk, fmt, ap);
  if (size > 0 && buf != 0) {
    size_t nul_at = (sk.used < out_cap) ? sk.used : out_cap;
    buf[nul_at] = '\0';
  }
  return full_len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return r;
}

/* ---- eof / err ----------------------------------------------------------- */

int feof(FILE *fp)   { return (fp != 0 && fp->eof_flag) ? 1 : 0; }
int ferror(FILE *fp) { return (fp != 0 && fp->err_flag) ? 1 : 0; }
