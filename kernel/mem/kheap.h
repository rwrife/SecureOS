/**
 * @file kheap.h
 * @brief Kernel heap allocator interface.
 *
 * Purpose:
 *   Provides kmalloc() and kfree() for dynamic kernel memory allocation.
 *   Uses a simple free-list allocator over a statically-allocated BSS arena.
 *   Designed for allocations that don't fit in fixed-size static tables
 *   (e.g., virtual framebuffers for the window manager).
 *
 * Interactions:
 *   - core/kmain.c: calls kheap_init() early in boot.
 *   - core/session_manager.c: allocates virtual framebuffers.
 *   - Any kernel code needing dynamic allocation.
 *
 * Launched by:
 *   kheap_init() must be called once from kmain(). Not standalone.
 */

#ifndef SECUREOS_KHEAP_H
#define SECUREOS_KHEAP_H

#include <stddef.h>

/**
 * Initialize the kernel heap. Must be called once at boot before any
 * kmalloc/kfree calls.
 */
void kheap_init(void);

/**
 * Allocate `size` bytes from the kernel heap.
 * Returns a pointer aligned to 16 bytes, or NULL if out of memory.
 * Returned memory is zero-initialized.
 */
void *kmalloc(size_t size);

/**
 * Free a previously allocated block. Passing NULL is a no-op.
 */
void kfree(void *ptr);

/**
 * Return the total number of bytes currently free in the heap.
 * Useful for diagnostics.
 */
size_t kheap_free_bytes(void);

/**
 * Return the total heap arena size in bytes.
 */
size_t kheap_total_bytes(void);

#endif /* SECUREOS_KHEAP_H */
