#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>
#include <kernel/types.h>
#include <kernel/vmm.h>

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

/** All returned addresses are aligned on a 32B boundary */
#define KMALLOC_ALIGNMENT (32)

/**
 * @brief Magic value to detect if a block is free
 *
 * To prevent corrupting the metadata by freeing the same block multiple times
 * we write this number right after the linked list node when freeing a block.
 * We then check for this arbitrary value before freeing it. If it's present
 * this means we're freeing an already free block.
 */
#define KMALLOC_FREE_MAGIC (0x3402CECE)
#define BLOCK_FREE_MAGIC(_block) \
    ((uint32_t *)(((void *)_block) + sizeof(node_t)))

/**
 * @brief The metadata for a single bucket
 * @struct bucket_meta
 *
 * A bucket's metadata is stored at the beginning of its mapped area,
 * inside its first block.
 *
 */
typedef struct bucket_meta {
    u32 block_size;  ///< The size of each blocks inside this bucket
    u16 block_count; ///< Number of blocks currently malloc'd
    u16 flags;       ///< Flags for this bucket
    llist_t free;    ///< Head of the freelist
    node_t this;
    char data[] __attribute__((aligned(KMALLOC_ALIGNMENT)));
} bucket_t;

static_assert(sizeof(bucket_t) <= KMALLOC_ALIGNMENT, "Bucket metadata MUST fit "
                                                     "into a single block");

static inline struct bucket_meta *to_bucket(node_t *this)
{
    return container_of(this, bucket_t, this);
}

static inline size_t bucket_compute_size(size_t block_size)
{
    return align_up(KMALLOC_ALIGNMENT + block_size, PAGE_SIZE);
}

/** Find a bucket containing with at least one free block of the given size */
static bucket_t *bucket_find(llist_t *buckets, size_t size, const u16 flags)
{
    FOREACH_LLIST (node, buckets) {
        bucket_t *bucket = to_bucket(node);
        if (bucket->block_size == size && !llist_is_empty(&bucket->free) &&
            bucket->flags == flags)
            return bucket;
    }

    return NULL;
}

/** Reserve a free block inside a bucket */
static void *bucket_get_free_block(bucket_t *bucket)
{
    void *block = llist_pop(&bucket->free);
    bucket->block_count += 1;
    // remove KMALLOC_FREE_MAGIC
    *BLOCK_FREE_MAGIC(block) = 0x0;
    return block;
}

/** Create a new empty bucket for blocks of size @c block_size */
static struct bucket_meta *bucket_create(struct address_space *as,
                                         llist_t *buckets, size_t block_size,
                                         const u16 flags)
{
    size_t bucket_size = bucket_compute_size(block_size);
    bucket_t *bucket = vm_alloc(as, bucket_size, VM_READ | VM_WRITE);

    if (IS_ERR(bucket))
        return NULL;

    bucket->block_size = block_size;
    bucket->block_count = 0;
    bucket->flags = flags;

    INIT_LLIST(bucket->free);

    // Generate the intrusive freelist
    node_t *node = (node_t *)bucket->data;
    size_t nb_blocks = (bucket_size - sizeof(bucket_t)) / block_size;
    for (size_t i = 0; i < nb_blocks; ++i) {
        *BLOCK_FREE_MAGIC(node) = KMALLOC_FREE_MAGIC;
        llist_add(&bucket->free, node);
        node = (void *)node + block_size;
    }

    llist_add(buckets, &bucket->this);

    return bucket;
}

/** Free a block inside a bucket */
static void
bucket_free_block(struct address_space *as, bucket_t *bucket, void *block)
{
    // Check if block is already free or not
    uint32_t *const block_free_magic = BLOCK_FREE_MAGIC(block);
    if (*block_free_magic == KMALLOC_FREE_MAGIC)
        return; // block is already free

    // If all blocks are free, unmap the bucket to avoid hording memory
    if (bucket->block_count == 1) {
        llist_remove(&bucket->this);
        vm_free(as, bucket);
        return;
    }

    *block_free_magic = KMALLOC_FREE_MAGIC;
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
    struct address_space *as;
    llist_t *buckets;

    if (size == 0)
        return NULL;

    as = (flags & KMALLOC_KERNEL) ? &kernel_address_space
                                  : current->process->as;
    buckets = &as->kmalloc;

    size = align_up(size, KMALLOC_ALIGNMENT);
    size = bit_next_pow32(size);

    bucket_t *bucket = bucket_find(buckets, size, flags);
    if (bucket == NULL)
        bucket = bucket_create(as, buckets, size, flags);

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
    struct address_space *as;

    if (ptr == NULL)
        return;

    as = IS_KERNEL_ADDRESS(ptr) ? &kernel_address_space : current->process->as;
    bucket_free_block(as, bucket_from_block(ptr), ptr);
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
    if (new != NULL)
        memcpy(new, ptr, size);

    kfree(ptr);

    return new;
}

void *krealloc_array(void *ptr, size_t nmemb, size_t size, int flags)
{
    if (__builtin_mul_overflow(nmemb, size, &size))
        return ptr;

    return krealloc(ptr, size, flags);
}

void *kmalloc_dma(size_t size)
{
    paddr_t physical;
    void *ptr;

    size = align_up(size, PAGE_SIZE);

    physical = pmm_allocate_pages(size);
    if (physical == PMM_INVALID_PAGEFRAME)
        return NULL;

    ptr = vm_alloc_at(&kernel_address_space, physical, size,
                      VM_READ | VM_WRITE);

    if (IS_ERR(ptr))
        return NULL;
    return ptr;
}

void kfree_dma(void *dma_ptr)
{
    if (!PAGE_ALIGNED(dma_ptr)) {
        log_err("kfree_dma: address is not the start of a page: %p", dma_ptr);
        return;
    }

    vm_free(&kernel_address_space, dma_ptr);
}
