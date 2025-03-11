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

/** Head of the linked list of buckets */
static DECLARE_LLIST(kmalloc_buckets);

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

/** Find a bucket containing with at least one free block of the given size */
static bucket_t *bucket_find(llist_t buckets, size_t size, const u16 flags)
{
    FOREACH_LLIST (node, buckets) {
        bucket_t *bucket = container_of(node, bucket_t, this);
        if (bucket->block_size == size && bucket->free != NULL &&
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
static struct bucket_meta *
bucket_create(llist_t *buckets, size_t block_size, const u16 flags)
{
    size_t bucket_size = align_up(KMALLOC_ALIGNMENT + block_size, PAGE_SIZE);
    bucket_t *bucket = mmap(NULL, bucket_size, PROT_READ | PROT_WRITE,
                            MAP_CLEAR | flags);

    if (bucket == NULL)
        return NULL;

    bucket->block_size = block_size;
    bucket->block_count = 0;
    bucket->flags = flags;

    // Generate the intrusive freelist
    node_t *node = (node_t *)bucket->data;
    size_t nb_blocks = (bucket_size - sizeof(bucket_t)) / block_size;
    for (size_t i = 0; i < nb_blocks - 1; ++i) {
        *BLOCK_FREE_MAGIC(node) = KMALLOC_FREE_MAGIC;
        node = node->next = (void *)node + block_size;
    }
    bucket->free = (llist_t)bucket->data;

    llist_add(buckets, &bucket->this);

    return bucket;
}

/** Free a block inside a bucket */
static void bucket_free_block(bucket_t *bucket, void *block, llist_t *buckets)
{
    // Check if block is already free or not
    uint32_t *const block_free_magic = BLOCK_FREE_MAGIC(block);
    if (*block_free_magic == KMALLOC_FREE_MAGIC)
        return; // block is already free

    // If all blocks are free, unmap the bucket to avoid hording memory
    if (bucket->block_count == 1) {
        llist_remove(buckets, &bucket->this);
        munmap(bucket,
               align_up(KMALLOC_ALIGNMENT + bucket->block_size, PAGE_SIZE));
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
    if (size == 0)
        return NULL;

    size = align_up(size, KMALLOC_ALIGNMENT);
    size = bit_next_pow32(size);

    llist_t *buckets = (flags & KMALLOC_KERNEL) ? &kmalloc_buckets
                                                : &current->kmalloc;

    bucket_t *bucket = bucket_find(*buckets, size, flags);
    if (bucket == NULL)
        bucket = bucket_create(buckets, size, flags);

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

    bucket_free_block(bucket_from_block(ptr), ptr,
                      IS_KERNEL_ADDRESS(ptr) ? &kmalloc_buckets
                                             : &current->kmalloc);
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

void *krealloc_carray(void *ptr, size_t nmemb, size_t size, int flags)
{
    if (__builtin_mul_overflow(nmemb, size, &size))
        return ptr;

    return krealloc(ptr, size, flags);
}

void *kmalloc_at(paddr_t phys, size_t size)
{
    vaddr_t virt;

    size = align_up(size, PAGE_SIZE);

    virt = vmm_allocate(&kernel_vmm, 0, size, VMA_KERNEL);
    if (!virt)
        return NULL;

    /* TODO: Forced to be rw. Should be modified when revamping the alloc API
     *       The flag system is currently a huge mess of
     * un-necessary/unused/leaked values. Even I am not sure what flag should be
     * used ... FFS please rework this mess!
     */
    if (!mmu_map_range(virt, phys, size, PROT_READ | PROT_WRITE | PROT_KERNEL))
        return NULL;

    return (void *)virt;
}

void *kmalloc_dma(size_t size)
{
    paddr_t physical;

    physical = pmm_allocate_pages(size, PMM_MAP_KERNEL);
    if (physical == PMM_INVALID_PAGEFRAME)
        return NULL;

    return kmalloc_at(physical, size);
}

void *kmalloc_dma_at(paddr_t dma_buffer, size_t size)
{
    return kmalloc_at(dma_buffer, size);
}

void kfree_at(void *ptr, size_t size)
{
    vaddr_t virt = (vaddr_t)ptr;

    if (ptr == NULL)
        return;

    mmu_unmap_range(virt, virt + size);
    vmm_free(&kernel_vmm, virt, size);
}

void kfree_dma(void *dma_ptr, size_t size)
{
    vaddr_t virt = (vaddr_t)dma_ptr;
    paddr_t phys;

    if (dma_ptr == NULL)
        return;

    phys = mmu_find_physical(virt);
    if (IS_ERR(phys))
        return;

    kfree_at(dma_ptr, size);
    pmm_free_pages(phys, size);
}
