#define LOG_DOMAIN "kmalloc"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/memory/slab.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/vm.h>

#include <string.h>

static struct kmem_cache *kmalloc_size_caches[KMALLOC_CACHE_COUNT];

static const char *kmalloc_cache_names[] = {
    "size-16",   "size-32",   "size-64",    "size-128",
    "size-256",  "size-512",  "size-1024",  "size-2048",
    "size-4096", "size-8192", "size-16384",
};

void *kmalloc_from_cache(int cache_index, int flags)
{
    return kmem_cache_alloc(kmalloc_size_caches[cache_index], flags);
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
    struct page *page;
    paddr_t paddr;

    if (ptr == NULL)
        return;

    paddr = mmu_find_physical((vaddr_t)ptr);
    page = address_to_page(paddr);

    kmem_cache_free(page->slab.cache, ptr);
}

void *krealloc(void *ptr, size_t size, int flags)
{
    struct page *page;
    paddr_t paddr;

    if (ptr == NULL)
        return kmalloc(size, flags);

    paddr = mmu_find_physical((vaddr_t)ptr);
    page = address_to_page(paddr);

    if (!(page->flags & PAGE_SLAB)) {
        WARN("reallocating an invalid pointer: %p", ptr);
        return ptr;
    }

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    /* No need to reallocate, current slab object is large enough already. */
    if (page->slab.cache->obj_size >= size)
        return ptr;

    kfree(ptr);
    return kmalloc(size, flags);
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

    ptr = vm_alloc_at(&kernel_address_space, physical, size, VM_KERNEL_RW);

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

void kmalloc_api_init(void)
{
    struct kmem_cache *cache;
    size_t obj_size = KMALLOC_CACHE_MIN_SIZE;

    for (int i = 0; i < KMALLOC_CACHE_COUNT; ++i, obj_size <<= 1) {
        cache = kmem_cache_create(kmalloc_cache_names[i], obj_size, 16, NULL,
                                  NULL);
        if (!cache)
            PANIC("failed to init kmalloc cache: '%s'", kmalloc_cache_names[i]);

        kmalloc_size_caches[i] = cache;
    }
}
