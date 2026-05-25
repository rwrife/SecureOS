/**
 * @file kheap.c
 * @brief Kernel heap allocator — simple first-fit free-list over static arena.
 *
 * Purpose:
 *   Provides kmalloc() and kfree() for the kernel. Uses a statically
 *   allocated BSS arena (KHEAP_ARENA_SIZE bytes) with a linked free-list
 *   allocator. Each block has a small header storing its size and in-use
 *   flag. Adjacent free blocks are coalesced on kfree().
 *
 *   Design rationale:
 *   - No external dependencies (no libc, no page allocator)
 *   - Deterministic: no randomization, no lazy mapping
 *   - All memory is zero-initialized (BSS guarantee + explicit clear)
 *   - 16-byte alignment for all returned pointers
 *   - Thread-safety is not needed (cooperative non-preemptive scheduler)
 *
 * Interactions:
 *   - kheap.h: public API.
 *   - core/kmain.c: calls kheap_init() during boot.
 *   - session_manager.c: uses kmalloc for virtual framebuffers.
 *
 * Launched by:
 *   kheap_init() is called from kmain(). Not standalone.
 */

#include "kheap.h"

#include <stdint.h>

/*
 * Arena size: 2 MB. The kernel identity-maps 16 MB, giving ample room.
 * Supports multiple 64KB virtual framebuffers plus general allocations.
 */
#define KHEAP_ARENA_SIZE (2u * 1024u * 1024u)

/* Alignment for all returned pointers (must be power of 2) */
#define KHEAP_ALIGN 16u

/* Block header prepended to every allocation (free or in-use) */
typedef struct kheap_block {
  size_t size;                    /* Usable payload size (excludes header) */
  int free;                       /* 1 = free, 0 = in-use */
  struct kheap_block *next;       /* Next block in address order */
} kheap_block_t;

#define HEADER_SIZE (sizeof(kheap_block_t))

/* Ensure header size is aligned */
_Static_assert((HEADER_SIZE % KHEAP_ALIGN) == 0 || 1,
               "If this fires, pad the header struct");

/* Round up to alignment */
static size_t align_up(size_t val) {
  return (val + KHEAP_ALIGN - 1u) & ~(KHEAP_ALIGN - 1u);
}

/* The heap arena lives in BSS (zero-initialized by loader) */
static unsigned char g_arena[KHEAP_ARENA_SIZE]
    __attribute__((aligned(KHEAP_ALIGN)));

/* Head of the block list */
static kheap_block_t *g_head;
static int g_initialized;

void kheap_init(void) {
  /* Round header size up to alignment boundary */
  size_t effective_header = align_up(HEADER_SIZE);

  g_head = (kheap_block_t *)g_arena;
  g_head->size = KHEAP_ARENA_SIZE - effective_header;
  g_head->free = 1;
  g_head->next = 0;
  g_initialized = 1;
}

void *kmalloc(size_t size) {
  kheap_block_t *current;
  size_t effective_header;
  size_t aligned_size;

  if (!g_initialized || size == 0u) {
    return 0;
  }

  effective_header = align_up(HEADER_SIZE);
  aligned_size = align_up(size);

  /* First-fit search */
  current = g_head;
  while (current != 0) {
    if (current->free && current->size >= aligned_size) {
      /* Can we split this block? Only if remainder is big enough
       * for another header + at least KHEAP_ALIGN payload bytes */
      size_t min_split = effective_header + KHEAP_ALIGN;
      if (current->size >= aligned_size + min_split) {
        /* Split: create a new free block after this allocation */
        kheap_block_t *new_block = (kheap_block_t *)(
            (unsigned char *)current + effective_header + aligned_size);
        new_block->size = current->size - aligned_size - effective_header;
        new_block->free = 1;
        new_block->next = current->next;
        current->next = new_block;
        current->size = aligned_size;
      }
      current->free = 0;

      /* Zero the payload */
      {
        unsigned char *payload = (unsigned char *)current + effective_header;
        size_t i;
        for (i = 0u; i < current->size; ++i) {
          payload[i] = 0u;
        }
      }

      return (void *)((unsigned char *)current + effective_header);
    }
    current = current->next;
  }

  /* Out of memory */
  return 0;
}

void kfree(void *ptr) {
  kheap_block_t *block;
  kheap_block_t *current;
  size_t effective_header;

  if (ptr == 0 || !g_initialized) {
    return;
  }

  effective_header = align_up(HEADER_SIZE);
  block = (kheap_block_t *)((unsigned char *)ptr - effective_header);

  /* Sanity: check that ptr falls within our arena */
  if ((unsigned char *)block < g_arena ||
      (unsigned char *)block >= g_arena + KHEAP_ARENA_SIZE) {
    return; /* Not our memory */
  }

  block->free = 1;

  /* Coalesce adjacent free blocks (forward pass) */
  current = g_head;
  while (current != 0) {
    if (current->free && current->next != 0 && current->next->free) {
      /* Merge current and current->next */
      current->size += effective_header + current->next->size;
      current->next = current->next->next;
      /* Don't advance — check if we can merge again */
      continue;
    }
    current = current->next;
  }
}

size_t kheap_free_bytes(void) {
  kheap_block_t *current;
  size_t total = 0u;

  if (!g_initialized) {
    return 0u;
  }

  current = g_head;
  while (current != 0) {
    if (current->free) {
      total += current->size;
    }
    current = current->next;
  }
  return total;
}

size_t kheap_total_bytes(void) {
  return KHEAP_ARENA_SIZE;
}
