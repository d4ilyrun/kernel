#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/syscalls.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>
#include <utils/bits.h>
#include <utils/container_of.h>
#include <utils/macro.h>
#include <utils/math.h>

#include <string.h>

/**
 * @defgroup kmalloc_internals Kmalloc - Internals
 * @ingroup kmalloc
 *
 * Internal functions and structures used for managing buckets.
 *
 * @{
 */

/** All returned addresses are aligned on a 16B boundary */
#define KMALLOC_ALIGNMENT (16)

/** Head of the linked list of buckets */
static llist_t kmalloc_buckets = ((void *)0);
;

/**
 * @brief The metadata for a single bucket
 * @struct bucket_meta
 *
 * A bucket's metadata is stored inside at the beginning of its mapped area,
 * inside its first block.
 *
 */
typedef struct bucket_meta {
    size_t block_size;  ///< The size of each blocks inside this bucket
    size_t block_count; ///< Number of blocks currently malloc'd
    llist_t free;       ///< Head of the freelist
    llist_node_t this;
    char data[] __attribute__((aligned(KMALLOC_ALIGNMENT)));
} bucket_t;

static_assert(sizeof(bucket_t) <= KMALLOC_ALIGNMENT,
              "Bucket metadata MUST fit into a single block");

/** Find a bucket containing with at least one free block of the given size */
static bucket_t *bucket_find(llist_t buckets, size_t size)
{
    FOREACH_LLIST(node, buckets)
    {
        bucket_t *bucket = container_of(node, bucket_t, this);
        if (bucket->block_size == size && bucket->free != NULL)
            return bucket;
    }

    return NULL;
}

/** Reserve a free block inside a bucket */
static void *bucket_get_free_block(bucket_t *bucket)
{
    void *block = llist_pop(&bucket->free);
    bucket->block_count += 1;
    return block;
}

/** Create a new empty bucket for blocks of size @c block_size */
static struct bucket_meta *bucket_create(llist_t *buckets, size_t block_size)
{
    size_t bucket_size = align_up(KMALLOC_ALIGNMENT + block_size, PAGE_SIZE);
    bucket_t *bucket = mmap(NULL, bucket_size, PROT_READ | PROT_WRITE, 0);

    if (bucket == NULL)
        return NULL;

    bucket->block_size = block_size;
    bucket->block_count = 0;

    // Generate the intrusive freelist
    llist_node_t *node = (llist_node_t *)bucket->data;
    size_t nb_blocks = (bucket_size - sizeof(bucket_t)) / block_size;
    for (size_t i = 0; i < nb_blocks - 1; ++i)
        node = node->next = (void *)node + block_size;
    bucket->free = (llist_t)bucket->data;

    llist_add(buckets, &bucket->this);

    return bucket;
}

/** Free a block inside a bucket */
static void bucket_free_block(bucket_t *bucket, void *block, llist_t *buckets)
{
    // If all blocks are free, unmap the bucket to avoid hording memory
    if (bucket->block_count == 1) {
        llist_remove(buckets, &bucket->this);
        munmap(bucket,
               align_up(KMALLOC_ALIGNMENT + bucket->block_size, PAGE_SIZE));
        return;
    }

    llist_add(&bucket->free, block);
    bucket->block_count -= 1;
}

static ALWAYS_INLINE bucket_t *bucket_from_block(void *block)
{
    return (bucket_t *)align_down((u32)block, PAGE_SIZE);
}

/** @} */

void *kmalloc(size_t size, int flags)
{
    UNUSED(flags);

    if (size == 0)
        return NULL;

    size = align_up(size, KMALLOC_ALIGNMENT);
    size = bit_next_pow32(size);

    bucket_t *bucket = bucket_find(kmalloc_buckets, size);
    if (bucket == NULL)
        bucket = bucket_create(&kmalloc_buckets, size);

    if (bucket == NULL)
        return NULL;

    return bucket_get_free_block(bucket);
}

void *kcalloc(size_t nmemb, size_t size, int flags)
{
    if (__builtin_mul_overflow(nmemb, size, &size))
        return NULL;

    void *ptr = kmalloc(size, flags);
    if (ptr == NULL)
        return NULL;

    return memset(ptr, 0, size);
}

void kfree(void *ptr)
{
    if (ptr == NULL)
        return;

    bucket_free_block(bucket_from_block(ptr), ptr, &kmalloc_buckets);
}

void *krealloc(void *ptr, size_t size, int flags)
{
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    if (ptr == NULL)
        return kmalloc(size, flags);

    size = align_up(size, KMALLOC_ALIGNMENT);
    size = bit_next_pow32(size);

    // Reuse same block if it is large enough
    bucket_t *bucket = bucket_from_block(ptr);
    if (bucket->block_size >= size)
        return ptr;

    void *new = kmalloc(size, flags);
    memcpy(new, ptr, size);
    kfree(ptr);

    return new;
}

void *krealloc_carray(void *ptr, size_t nmemb, size_t size, int flags)
{
    if (__builtin_mul_overflow(nmemb, size, &size))
        return ptr;

    return krealloc(ptr, size, flags);
}
