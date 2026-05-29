/**
 * @file src/malloc.c
 * @brief Freestanding boundary-tag, first-fit allocator (issue #404).
 *
 * Design (deliberately small, deterministic, host-testable):
 *
 *   - The arena is one contiguous byte window the embedder owns. We
 *     never call sbrk/mmap directly; growth is mediated by the
 *     `clib_brk_fn` registered at init.
 *   - Each block has a header at its start and a footer at its end.
 *     The header carries `(size, in_use)` packed into a single
 *     `uintptr_t`: the low bit is `in_use`, the rest is the block
 *     size (header + payload + footer) — always a multiple of
 *     `CLIB_BLOCK_ALIGN`, so the low bit is free.
 *   - Free blocks live in a singly linked LIFO free list rooted at
 *     `g_free_head`. First-fit search; on free we coalesce with both
 *     physical neighbours via the footer/header tags.
 *   - All allocations are 16-byte aligned; the header is sized so the
 *     payload that follows naturally lands on a 16-byte boundary.
 *
 * What we deliberately do NOT do here (kept simple on purpose):
 *   - No size-class buckets, no thread safety, no debug-fill of
 *     freed memory. TinyCC is single-threaded and short-lived; the
 *     primary win is "does it work and stay simple enough to debug
 *     in-kernel".
 *   - No arena shrink. `free` returns to the free list; growth is
 *     one-way.
 */

#include "../include/clib/malloc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CLIB_BLOCK_ALIGN ((size_t)16)
#define CLIB_HEADER_SIZE ((size_t)16)  /* keeps payload 16-byte aligned */
#define CLIB_FOOTER_SIZE ((size_t)8)
#define CLIB_OVERHEAD    (CLIB_HEADER_SIZE + CLIB_FOOTER_SIZE)

/*
 * Block layout:
 *
 *   +--------- header (16 B) ---------+
 *   | size_and_flag    (uintptr_t)    |   low bit = in_use
 *   | free_list_next   (uintptr_t)    |   only meaningful when free
 *   +--------- payload (size - OVH)   |
 *   |   ...                           |
 *   +--------- footer (8 B)  ---------+
 *   | size_and_flag    (uintptr_t)    |   copy of header
 *   +---------------------------------+
 */
typedef struct clib_block {
  uintptr_t size_and_flag;
  uintptr_t free_next; /* only used when free; otherwise undefined */
} clib_block_t;

#define CLIB_BLOCK_IN_USE_BIT ((uintptr_t)1)
#define CLIB_BLOCK_SIZE_MASK  (~CLIB_BLOCK_IN_USE_BIT)

static uint8_t      *g_arena_base    = NULL;
static size_t        g_arena_size    = 0;
static clib_brk_fn   g_brk_fn        = NULL;
static void         *g_brk_ctx       = NULL;
static clib_block_t *g_free_head     = NULL;
static size_t        g_bytes_in_use  = 0;
static size_t        g_live_count    = 0;

static size_t round_up(size_t n, size_t a) {
  return (n + (a - 1u)) & ~(a - 1u);
}

static uintptr_t pack(size_t sz, bool in_use) {
  return (uintptr_t)sz | (in_use ? CLIB_BLOCK_IN_USE_BIT : 0u);
}

static size_t blk_size(const clib_block_t *b) {
  return (size_t)(b->size_and_flag & CLIB_BLOCK_SIZE_MASK);
}

static bool blk_in_use(const clib_block_t *b) {
  return (b->size_and_flag & CLIB_BLOCK_IN_USE_BIT) != 0;
}

static uintptr_t *blk_footer_at(uint8_t *p, size_t sz) {
  return (uintptr_t *)(p + sz - CLIB_FOOTER_SIZE);
}

static void blk_write_tags(clib_block_t *b, size_t sz, bool in_use) {
  b->size_and_flag = pack(sz, in_use);
  *blk_footer_at((uint8_t *)b, sz) = pack(sz, in_use);
}

static void *blk_payload(clib_block_t *b) {
  return (uint8_t *)b + CLIB_HEADER_SIZE;
}

static clib_block_t *payload_to_block(void *p) {
  return (clib_block_t *)((uint8_t *)p - CLIB_HEADER_SIZE);
}

static bool in_arena(const void *p) {
  if (!g_arena_base || !g_arena_size) {
    return false;
  }
  const uint8_t *u = (const uint8_t *)p;
  return u >= g_arena_base && u < g_arena_base + g_arena_size;
}

/* Push `b` to the front of the free list; assumes b is already tagged free. */
static void free_list_push(clib_block_t *b) {
  b->free_next = (uintptr_t)g_free_head;
  g_free_head = b;
}

/* Remove `target` from the free list. O(n) — fine for our scale. */
static void free_list_remove(clib_block_t *target) {
  clib_block_t **cur = &g_free_head;
  while (*cur) {
    if (*cur == target) {
      *cur = (clib_block_t *)target->free_next;
      target->free_next = 0;
      return;
    }
    cur = (clib_block_t **)&(*cur)->free_next;
  }
}

/*
 * Initialise the arena window into one giant free block. Caller has
 * already validated that `size >= clib_malloc_min_seed_bytes()`.
 */
static void seed_initial_block(uint8_t *base, size_t size) {
  clib_block_t *b = (clib_block_t *)base;
  blk_write_tags(b, size, /*in_use=*/false);
  b->free_next = 0;
  g_free_head = b;
}

/*
 * Try to extend the arena by at least `wanted` payload bytes. Returns
 * a pointer to a free block that holds at least `wanted` payload, or
 * NULL on brk failure.
 *
 * If the existing tail block is free, we ask brk for just the
 * shortfall and fold the new bytes into the tail. Otherwise we ask for
 * a fresh free block.
 */
static clib_block_t *grow_arena(size_t wanted_payload) {
  if (!g_brk_fn) {
    return NULL;
  }

  /* Compute total bytes (header+payload+footer), rounded to alignment. */
  size_t need_block = round_up(wanted_payload + CLIB_OVERHEAD,
                               CLIB_BLOCK_ALIGN);

  /* Find tail block by walking; the arena is contiguous so this is just
   * a forward scan. Small N today; if it ever matters we cache a tail
   * pointer. */
  clib_block_t *tail = NULL;
  if (g_arena_base && g_arena_size) {
    uint8_t *p = g_arena_base;
    while (p < g_arena_base + g_arena_size) {
      clib_block_t *b = (clib_block_t *)p;
      tail = b;
      p += blk_size(b);
    }
  }

  bool fold = (tail != NULL && !blk_in_use(tail));
  size_t fold_room = fold ? blk_size(tail) : 0;
  size_t delta = need_block;
  if (fold && fold_room < need_block) {
    delta = round_up(need_block - fold_room, CLIB_BLOCK_ALIGN);
  } else if (fold) {
    /* Tail free block already fits — no brk needed. */
    free_list_remove(tail);
    blk_write_tags(tail, fold_room, /*in_use=*/false);
    tail->free_next = 0;
    free_list_push(tail);
    return tail;
  }

  void *got = g_brk_fn(g_brk_ctx, delta);
  if (!got) {
    return NULL;
  }

  /* The brk contract requires contiguity. Sanity-check it: the new
   * region must start exactly at the old end. If it doesn't, we treat
   * it as a brk failure (no panic, just NULL — matches the issue's
   * "fails cleanly" acceptance). */
  uint8_t *expected_end = (g_arena_base
                           ? g_arena_base + g_arena_size
                           : (uint8_t *)got);
  if (!g_arena_base) {
    g_arena_base = (uint8_t *)got;
  } else if ((uint8_t *)got != expected_end) {
    /* Non-contiguous brk: refuse to grow. The arena bytes returned are
     * "lost" from the allocator's view but the embedder still owns
     * them — no leak from our perspective, the brk shim's problem. */
    return NULL;
  }
  g_arena_size += delta;

  if (fold) {
    /* Extend the tail free block in place. */
    free_list_remove(tail);
    size_t new_size = fold_room + delta;
    blk_write_tags(tail, new_size, /*in_use=*/false);
    tail->free_next = 0;
    free_list_push(tail);
    return tail;
  }

  /* Fresh free block at the new tail. */
  clib_block_t *nb = (clib_block_t *)((uint8_t *)got);
  blk_write_tags(nb, delta, /*in_use=*/false);
  nb->free_next = 0;
  free_list_push(nb);
  return nb;
}

/*
 * Split `b` so it holds exactly `want` total bytes (header+payload+
 * footer); the remainder becomes a new free block on the free list,
 * but only if the remainder is at least one full minimum-sized block.
 */
static void split_block(clib_block_t *b, size_t want) {
  size_t have = blk_size(b);
  size_t min_block = CLIB_OVERHEAD + CLIB_BLOCK_ALIGN;
  if (have < want + min_block) {
    /* Not enough room to split — give the whole block to the caller. */
    return;
  }
  size_t rem = have - want;
  /* Resize current block. */
  blk_write_tags(b, want, /*in_use=*/false);
  /* Carve remainder. */
  clib_block_t *r = (clib_block_t *)((uint8_t *)b + want);
  blk_write_tags(r, rem, /*in_use=*/false);
  r->free_next = 0;
  free_list_push(r);
}

/* ---------------- public API ---------------- */

size_t clib_malloc_min_seed_bytes(void) {
  /* One smallest allocation + bookkeeping, rounded up to alignment so
   * the seed window itself is always a whole number of blocks. */
  return round_up(CLIB_OVERHEAD + CLIB_BLOCK_ALIGN, CLIB_BLOCK_ALIGN);
}

int clib_malloc_init(void *seed_base,
                     size_t seed_size,
                     clib_brk_fn brk_fn,
                     void *brk_ctx) {
  /* Reset all state — this IS the second-run isolation contract. */
  g_arena_base   = NULL;
  g_arena_size   = 0;
  g_brk_fn       = brk_fn;
  g_brk_ctx      = brk_ctx;
  g_free_head    = NULL;
  g_bytes_in_use = 0;
  g_live_count   = 0;

  if (seed_size > 0 && !seed_base) {
    return -1;
  }
  if (seed_size > 0 && seed_size < clib_malloc_min_seed_bytes()) {
    return -1;
  }
  /* The seed window must be 16-byte aligned at both ends so the
   * boundary tags fall on natural boundaries. */
  if (seed_base &&
      (((uintptr_t)seed_base & (CLIB_BLOCK_ALIGN - 1u)) != 0 ||
       (seed_size & (CLIB_BLOCK_ALIGN - 1u)) != 0)) {
    return -1;
  }

  if (seed_size > 0) {
    g_arena_base = (uint8_t *)seed_base;
    g_arena_size = seed_size;
    seed_initial_block(g_arena_base, g_arena_size);
  }
  return 0;
}

void clib_malloc_shutdown(void) {
  g_arena_base   = NULL;
  g_arena_size   = 0;
  g_brk_fn       = NULL;
  g_brk_ctx      = NULL;
  g_free_head    = NULL;
  g_bytes_in_use = 0;
  g_live_count   = 0;
}

void *clib_malloc(size_t size) {
  if (size == 0) {
    /* C standard allows malloc(0) to return NULL or a unique pointer.
     * We return NULL — simpler, callers must handle it anyway. */
    return NULL;
  }
  size_t payload = round_up(size, CLIB_BLOCK_ALIGN);
  size_t need    = payload + CLIB_OVERHEAD;

  /* First-fit walk. */
  clib_block_t **prev = &g_free_head;
  clib_block_t  *cur  = g_free_head;
  while (cur) {
    if (blk_size(cur) >= need) {
      /* Detach from free list. */
      *prev = (clib_block_t *)cur->free_next;
      cur->free_next = 0;
      split_block(cur, need);
      /* split_block may have left `cur` smaller; re-read. */
      size_t final_sz = blk_size(cur);
      blk_write_tags(cur, final_sz, /*in_use=*/true);
      g_bytes_in_use += final_sz - CLIB_OVERHEAD;
      g_live_count   += 1;
      return blk_payload(cur);
    }
    prev = (clib_block_t **)&cur->free_next;
    cur  = (clib_block_t *)cur->free_next;
  }

  /* Nothing fits — try to grow. */
  clib_block_t *grown = grow_arena(payload);
  if (!grown) {
    return NULL;
  }
  /* The newly free block is on the free list. Retry exactly once. */
  if (blk_size(grown) < need) {
    return NULL; /* should not happen; defensive */
  }
  free_list_remove(grown);
  split_block(grown, need);
  size_t final_sz = blk_size(grown);
  blk_write_tags(grown, final_sz, /*in_use=*/true);
  g_bytes_in_use += final_sz - CLIB_OVERHEAD;
  g_live_count   += 1;
  return blk_payload(grown);
}

void clib_free(void *ptr) {
  if (!ptr) {
    return;
  }
  if (!in_arena(ptr)) {
    /* Foreign pointer — refuse silently rather than corrupt state. */
    return;
  }
  clib_block_t *b = payload_to_block(ptr);
  if (!blk_in_use(b)) {
    /* Double-free — refuse silently. */
    return;
  }
  size_t sz = blk_size(b);
  g_bytes_in_use -= (sz - CLIB_OVERHEAD);
  g_live_count   -= 1;
  blk_write_tags(b, sz, /*in_use=*/false);
  b->free_next = 0;

  /* Coalesce with next physical block if free. */
  uint8_t *next_addr = (uint8_t *)b + sz;
  if (next_addr < g_arena_base + g_arena_size) {
    clib_block_t *n = (clib_block_t *)next_addr;
    if (!blk_in_use(n)) {
      free_list_remove(n);
      sz += blk_size(n);
      blk_write_tags(b, sz, /*in_use=*/false);
    }
  }

  /* Coalesce with previous physical block if free. */
  if ((uint8_t *)b > g_arena_base) {
    uintptr_t *prev_foot = (uintptr_t *)((uint8_t *)b - CLIB_FOOTER_SIZE);
    if (((*prev_foot) & CLIB_BLOCK_IN_USE_BIT) == 0) {
      size_t prev_sz = (size_t)((*prev_foot) & CLIB_BLOCK_SIZE_MASK);
      clib_block_t *p = (clib_block_t *)((uint8_t *)b - prev_sz);
      free_list_remove(p);
      size_t merged = prev_sz + sz;
      blk_write_tags(p, merged, /*in_use=*/false);
      p->free_next = 0;
      free_list_push(p);
      return;
    }
  }

  free_list_push(b);
}

void *clib_realloc(void *ptr, size_t size) {
  if (!ptr) {
    return clib_malloc(size);
  }
  if (size == 0) {
    clib_free(ptr);
    return NULL;
  }
  if (!in_arena(ptr)) {
    return NULL;
  }
  clib_block_t *b = payload_to_block(ptr);
  if (!blk_in_use(b)) {
    return NULL;
  }
  size_t old_payload = blk_size(b) - CLIB_OVERHEAD;
  if (size <= old_payload) {
    /* Shrink in place; don't bother splitting (keeps the path simple). */
    return ptr;
  }
  void *np = clib_malloc(size);
  if (!np) {
    return NULL;
  }
  /* Copy old contents (manual memcpy — freestanding). */
  uint8_t *src = (uint8_t *)ptr;
  uint8_t *dst = (uint8_t *)np;
  for (size_t i = 0; i < old_payload; i++) {
    dst[i] = src[i];
  }
  clib_free(ptr);
  return np;
}

void *clib_calloc(size_t nmemb, size_t size) {
  if (nmemb && size > (SIZE_MAX / nmemb)) {
    return NULL; /* overflow */
  }
  size_t total = nmemb * size;
  void *p = clib_malloc(total);
  if (!p) {
    return NULL;
  }
  uint8_t *u = (uint8_t *)p;
  for (size_t i = 0; i < total; i++) {
    u[i] = 0;
  }
  return p;
}

void clib_malloc_get_stats(clib_malloc_stats_t *out) {
  if (!out) {
    return;
  }
  out->bytes_in_use = g_bytes_in_use;
  out->bytes_total  = g_arena_size;
  out->live_alloc_count = g_live_count;
  size_t fc = 0;
  for (clib_block_t *c = g_free_head; c; c = (clib_block_t *)c->free_next) {
    fc++;
  }
  out->free_block_count = fc;
}
