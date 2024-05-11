#pragma once

/**
 * @brief Dynamic allocator for the kernel
 *
 * @file kernel/kmalloc.h
 *
 * @defgroup kmalloc Kernel dynamic allocator
 * @ingroup kernel
 *
 * # Kmalloc
 *
 * Kmalloc is the dynamic memory allocator used inside our kernel.
 *
 * This must not be confused with malloc, which is the dynamic allocator
 * implementation, provided by the libc, used inside user programs.
 *
 * ## Design
 *
 * The @c kmalloc allocator uses a bucket allocator design.
 *
 * The idea is to have a mapped space, called a bucket, divided into blocks of
 * the same given size. The size of these blocks is always a power of 2 (8
 * bytes, 16 bytes, etc.).
 *
 * When the kernel wants memory, @c kmalloc chooses the bucket with the smallest
 * block size that can fit the data. There is no metadata stored with the data
 * inside the block, instead a metadata structure is kept at the beginning of
 * each bucket. The free blocks are retrieved using an intrusive linked list,
 * called freelist, whose head is kept inside the bucket's metadata.
 *
 * ## Advantages
 *
 * * Easy to implement
 * * RCU compatible, thanks to linked-lists
 * * No memory fragmentation
 * * Fast allocation
 *
 * ## Downsides
 *
 * * Not the most efficient memory-wise
 * * No caching for big structures like a slab allocator
 *
 * ### Notes
 *
 * * The minimum size of a block is 16B
 * * All addresses returned by this allocator are aligned onto a 16B boundary
 *
 * @{
 */

#include <stddef.h>

/**
 * @enum kmalloc_flags
 * @brief Feature flags passed to the kmalloc function family
 */
typedef enum kmalloc_flags {
    KMALLOC_DEFAULT = 0x0, ///< Default
} kmalloc_flags;

/**
 * Allocate @c size bytes and return a pointer to the allocated memory.
 *
 * An area allocated using this function MUST be freed manually.
 *
 * @param size The number of bytes to allocate
 * @param flags Feature flags, must be a combination of @ref kmalloc_flags
 *
 * @return The starting address of the newly allocated area
 */
void *kmalloc(size_t size, int flags);

/**
 * Allocate @c nmemb members of @c size bytes and initialize its content to 0.
 *
 * @param nmemb The number of members to allocate
 * @param size The size of each members
 * @param flags Feature flags, must be a combination of @ref kmalloc_flags
 *
 * @return The starting address of the newly allocated area
 */
void *kcalloc(size_t nmemb, size_t size, int flags);

/** Free a pointer allocated through @ref kmalloc */
void kfree(void *ptr);

/**
 * Change the size of the given memory block.
 *
 * The content of the old memory block is transfered into the first fitting
 * bytes of the new area, and the old one is automatically free'd.
 *
 * The new area returned by this function MUST be freed manually.
 *
 * This function can also be called to reallocate a memory block using different
 * feature flags.
 *
 * @param ptr Starting address of the memory block
 * @param size New size of the memory block
 * @param flags New flags of the memory block
 *
 * @return The starting address of the relocated area
 */
void *krealloc(void *ptr, size_t size, int flags);

/**
 * Change the size of the given memory block.
 *
 * @see
 * @ref kcalloc
 * @ref krealloc
 */
void *krealloc_carray(void *ptr, size_t nmemb, size_t size, int flags);
