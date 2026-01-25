#define LOG_DOMAIN "dma"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory/dma.h>
#include <kernel/memory/slab.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/vm.h>

static struct kmem_cache *kmalloc_dma_size_caches[KMALLOC_CACHE_COUNT];

static const char *kmalloc_dma_cache_names[] = {
    "dma-16",   "dma-32",   "dma-64",   "dma-128",
    "dma-256",  "dma-512",  "dma-1024", "dma-2048",
    "dma-4096", "dma-8192", "dma-16384",
};

/*
 *
 */
static inline void *kmalloc_dma_from_cache(unsigned int index)
{
    return kmem_cache_alloc(kmalloc_dma_size_caches[index], 0);
}

/*
 *
 */
static inline void *kmalloc_dma_large(size_t size)
{
    void *ptr;

    ptr = kmalloc_large(size, KMALLOC_KERNEL);
    if (!ptr)
        return ptr;

    /*
     * Configure the caching policies required for DMA on platforms that do not
     * support flushing inidividual cache lines.
     */
    if (!cpuinfo->cache_flush_available)
        vm_set_policy(&kernel_address_space, ptr, VM_CACHE_UC);

    return ptr;
}

/*
 *
 */
void *kmalloc_dma(size_t size)
{
    int cache_index = kmalloc_cache_index(size);

    if (unlikely(cache_index < 0))
        return kmalloc_dma_large(size);

    return kmalloc_dma_from_cache(cache_index);
}

/*
 *
 */
void kfree_dma(void *ptr)
{
    kfree(ptr);
}

/*
 *
 */
void dma_api_init(void)
{
    struct kmem_cache *cache;
    size_t obj_size = KMALLOC_CACHE_MIN_SIZE;
    enum kmem_cache_create_flags cache_flags = 0;

    /*
     * On platforms that do not support flushing individual cachelines DMA
     * buffers should be mapped uncacheable so that the device's view of the
     * memory is coherent with ours during and after a transfer.
     *
     * TODO: This should instead depend on the target device's hardware
     *       cache-coherency support.
     */
    if (!cpuinfo->cache_flush_available)
        cache_flags |= KMEM_CACHE_UNCACHEABLE | KMEM_CACHE_EXTERNAL;

    for (int i = 0; i < KMALLOC_CACHE_COUNT; ++i, obj_size <<= 1) {
        cache = kmem_cache_create(kmalloc_dma_cache_names[i], obj_size, 16,
                                  NULL, NULL, cache_flags);
        if (!cache)
            PANIC("failed to init kmalloc cache: '%s'",
                  kmalloc_dma_cache_names[i]);

        kmalloc_dma_size_caches[i] = cache;
    }
}
