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

#pragma once

#include <kernel/types.h>

#include <utils/compiler.h>

/**
 * @enum kmalloc_flags
 * @brief Feature flags passed to the kmalloc function family
 */
typedef enum kmalloc_flags {
    KMALLOC_KERNEL = 0, /* Default allocation flags. */
} kmalloc_flags_t;

#define KMALLOC_CACHE_MIN_SIZE 16
#define KMALLOC_CACHE_MAX_SIZE 16384
#define KMALLOC_CACHE_COUNT 11

/*
 * @return The index of the smallest cache that can contain a @size bytes object
 *
 * This function is inlined and marked 'const' so that it is optimized out by
 * the compiler when passing a value known at compile time.
 */
static ALWAYS_INLINE __attribute__((const)) int kmalloc_cache_index(size_t size)
{
    if (size <= 16) return 0;
    if (size <= 32) return 1;
    if (size <= 64) return 2;
    if (size <= 128) return 3;
    if (size <= 256) return 4;
    if (size <= 512) return 5;
    if (size <= 1024) return 6;
    if (size <= 2048) return 7;
    if (size <= 4096) return 8;
    if (size <= 8192) return 9;
    if (size <= 16384) return 10;

    return -1;
}

/** Allocate kernel memory from one of the global memory caches.
 *
 * @see kmalloc_cache_index() to know which cache_index to use.
 *
 * @param cache_index Index of the cache to allocate an object from.
 * @param flags Allocation flags to use.
 */
void *kmalloc_from_cache(int cache_index, int flags);

/** Allocate a memory buffer too large to fit inside the default caches. */
void *kmalloc_large(size_t size, int flags);

/*
 *
 */
static ALWAYS_INLINE void *kmalloc(size_t size, int flags)
{
    int cache_index;

    cache_index = kmalloc_cache_index(size);
    if (cache_index < 0)
        return kmalloc_large(size, flags);

    return kmalloc_from_cache(cache_index, flags);
}


/**
 * Allocate @c nmemb members of @c size bytes and initialize its content to 0.
 *
 * @param nmemb The number of members to allocate
 * @param size The size of each members
 * @param flags Feature flags, must be a combination of @ref kmalloc_flags_t
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
void *krealloc_array(void *ptr, size_t nmemb, size_t size, int flags);

/**
 * Allocate an addressable memory buffer suitable for DMA operations
 *
 * @param size The size of the buffer
 *
 * @note DMA regions are by design always located inside the kernel's space
 *
 * @return A buffer whose physical pageframes are contiguous
 */
void *kmalloc_dma(size_t size);

/** Free a buffer allocated through @ref kmalloc_dma */
void kfree_dma(void *dma_ptr);

void kmalloc_api_init(void);

/** @} */
