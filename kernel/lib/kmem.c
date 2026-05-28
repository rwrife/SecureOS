/**
 * @file kmem.c
 * @brief Freestanding memcpy/memset/memmove/memcmp shims for the kernel.
 *
 * Purpose:
 *   The kernel is compiled `-ffreestanding` with no libc. Most kernel TUs
 *   either roll their own per-file static helpers (e.g. `ipc_port_memcpy`,
 *   `vnet_memset`) or avoid bulk memory ops entirely. However, clang
 *   (even in freestanding mode) is free to lower aggregate assignments
 *   such as `*out = arr[i];` (struct copy) into a call to `memcpy`, and
 *   it lowers zero-initialised local aggregates into calls to `memset`.
 *
 *   Without a symbol named `memcpy` (or `memset` / `memmove` / `memcmp`)
 *   in the link, those auto-lowerings produce undefined-reference errors
 *   at `ld.lld` time. This TU provides minimal, byte-wise implementations
 *   so the kernel ELF links regardless of which TUs do or do not have
 *   their own bulk-mem helpers.
 *
 * Notes:
 *   - These functions match the standard C library signatures so the
 *     compiler-generated calls resolve directly.
 *   - Implementations are intentionally simple/byte-wise. They are not
 *     hot paths today; if/when a kernel hot path needs throughput we
 *     can specialise without changing the API.
 *   - Marked with the `used` attribute to defeat any dead-code elimination
 *     that might fire on link-time GC if it ever gets enabled.
 */

#include <stddef.h>

#define KMEM_USED __attribute__((used))

KMEM_USED
void *memcpy(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dest;
}

KMEM_USED
void *memmove(void *dest, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  if (d == s || n == 0) {
    return dest;
  }
  if (d < s) {
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  return dest;
}

KMEM_USED
void *memset(void *dest, int value, size_t n) {
  unsigned char *d = (unsigned char *)dest;
  unsigned char v = (unsigned char)value;
  for (size_t i = 0; i < n; i++) {
    d[i] = v;
  }
  return dest;
}

KMEM_USED
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *ua = (const unsigned char *)a;
  const unsigned char *ub = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    if (ua[i] != ub[i]) {
      return (int)ua[i] - (int)ub[i];
    }
  }
  return 0;
}
