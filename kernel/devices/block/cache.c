#define LOG_DOMAIN "cache"

#include <kernel/devices/block.h>
#include <kernel/devices/timer.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/vm.h>

#include <utils/macro.h>
#include <utils/math.h>

static DECLARE_LLIST(global_cached_pages);
static DECLARE_SPINLOCK(global_cached_pages_lock);

static inline struct page_cache_entry *to_cache_entry(const node_t *this)
{
    return container_of(this, struct page_cache_entry, this);
}

static inline struct block_device *cache_blkdev(const struct page_cache *cache)
{
    return container_of(cache, struct block_device, cache);
}

/*
 * Compare function used to find an entry inside the cache based on the index
 * of its first block.
 *
 * @see llist_find_first_sorted()
 */
static int __cache_entry_is(const void *this, const void *block)
{
    if ((blkcnt_t)block == to_cache_entry(this)->first_block)
        return COMPARE_EQ;

    return ((blkcnt_t)block <= to_cache_entry(this)->first_block)
             ? COMPARE_LESS
             : COMPARE_GREATER;
}

/*
 * Try and retreive the cached page that contains data for the block
 * at the given index. This function MUST be called with the cache's lock held.
 */
static struct page_cache_entry *
block_device_cache_find(struct block_device *blkdev, blkcnt_t block)
{
    struct page_cache *cache = &blkdev->cache;
    node_t *node;

    block = align_down(block, cache->blocks_per_page);
    node = llist_find_first_sorted(&cache->entries, (void *)block,
                                   __cache_entry_is);
    if (!node)
        return NULL;

    return to_cache_entry(node);
}

/*
 * Create a new cached page, copy the physical block into it and add it
 * to the cache. The caller MUST be holding the cache's lock and be sure
 * that no entry already exists for this block.
 */
static struct page_cache_entry *
block_device_cache_create(struct block_device *blkdev, blkcnt_t block)
{
    struct page_cache *cache = &blkdev->cache;
    struct page_cache_entry *entry = NULL;
    paddr_t paddr = PMM_INVALID_PAGEFRAME;
    struct page *page;
    void *buffer;
    error_t err;

    err = E_NOMEM;

    entry = kmalloc(sizeof(*entry), KMALLOC_KERNEL);
    if (!entry)
        goto cache_create_fail;

    paddr = pmm_allocate();
    if (paddr == PMM_INVALID_PAGEFRAME)
        goto cache_create_fail;

    buffer = vm_alloc_at(&kernel_address_space, paddr, PAGE_SIZE, VM_KERNEL_RW);
    if (!buffer)
        goto cache_create_fail;

    /* cache->blocks_per_page is a power of 2. */
    entry->first_block = align_down(block, cache->blocks_per_page);
    entry->buffer = buffer;
    entry->page = address_to_page(paddr);
    entry->cache = cache;
    entry->refcount = 0;

    /*
     * Initialize the cache entry by filling it with the content
     * of the physical blocks.
     */
    for (unsigned int i = 0; i < cache->blocks_per_page; ++i) {
        err = block_read_direct(blkdev, buffer, entry->first_block + i);
        if (err)
            goto cache_create_fail;
        buffer += blkdev->block_size;
    }

    if (!llist_insert_sorted_unique(&cache->entries, &entry->this,
                                    __cache_entry_is)) {
        err = E_EXIST;
        goto cache_create_fail;
    }

    locked_scope (&global_cached_pages_lock) {
        llist_add_tail(&global_cached_pages, &entry->this_global);
    }

    page = address_to_page(paddr);
    page->flags |= PAGE_VNODE;

    return entry;

cache_create_fail:
    if (paddr != PMM_INVALID_PAGEFRAME)
        pmm_free(paddr);
    kfree(entry);
    return PTR_ERR(err);
}

const struct page_cache_entry *
block_device_cache_get(struct block_device *blkdev, blkcnt_t block)
{
    struct page_cache *cache = &blkdev->cache;
    struct page_cache_entry *entry;

    spinlock_acquire(&cache->lock);

    entry = block_device_cache_find(blkdev, block);
    if (entry)
        goto cache_get_exit;

    entry = block_device_cache_create(blkdev, block);

cache_get_exit:
    entry->refcount += 1;
    spinlock_release(&cache->lock);
    return entry;
}

bool block_device_cache_put(struct block_device *blkdev,
                            struct page_cache_entry *entry)
{
    struct page_cache *cache = &blkdev->cache;
    error_t err;

    spinlock_acquire(&cache->lock);

    entry->refcount -= 1;
    if (entry->refcount > 0) {
        spinlock_release(&cache->lock);
        return false;
    }

    llist_remove(&entry->this);
    locked_scope (&global_cached_pages_lock) {
        llist_remove(&entry->this_global);
    }

    err = block_device_cache_writeback(entry);
    if (err)
        log_warn("%s: failed to flush cache entry %ld: %pE", blkdev->dev.name,
                 entry->first_block, &err);

    spinlock_release(&cache->lock);

    vm_free(&kernel_address_space, entry->buffer);
    kfree(entry);

    return true;
}

error_t block_device_cache_writeback(struct page_cache_entry *entry)
{
    struct page_cache *cache = entry->cache;
    struct block_device *blkdev = cache_blkdev(cache);
    void *buffer = entry->buffer;

    for (unsigned int i = 0; i < cache->blocks_per_page; ++i) {
        block_write_direct(blkdev, buffer, entry->first_block + i);
        buffer += blkdev->block_size;
    }

    return E_SUCCESS;
}

error_t block_device_cache_init(struct block_device *blkdev)
{
    struct page_cache *cache = &blkdev->cache;

    if (WARN_ON(blkdev->block_size > PAGE_SIZE)) {
        not_implemented("multiple pages cache");
        return E_NOT_SUPPORTED;
    }

    cache->blocks_per_page = PAGE_SIZE / blkdev->block_size;
    if (!is_power_of_two(cache->blocks_per_page))
        return E_INVAL;

    INIT_LLIST(cache->entries);
    INIT_SPINLOCK(cache->lock);

    return E_SUCCESS;
}

/*
 * Flush all global cached pages.
 *
 * This function is ran inside a dedicated kernel thread, and regularly flushes
 * all dirty cached buffers (that have been written to).
 */
static void block_device_cache_flush(void *cookie)
{
    struct page_cache_entry *entry;

    UNUSED(cookie);

    /*
     * TODO: Use a better scheduling algorithm than this one.
     *
     * By locking the whole global list, we end up blocking any thread
     * that may try to create a new entry in the cache until we are done.
     * This delay increases linearly with the number of global cached pages.
     *
     * We could use a 2 queues system, where we flushed all pages on one queue,
     * popping them and pushing them to the other queue until the first queue
     * becomes empty. Once the 'active' queue is empty both queues are switched.
     *
     * This system would allow us to push newly created pages onto the inactive
     * queue to avoid blocking other threads, and also limit the number of flush
     * performed by this thread each quanta.
     */
    while (true) {
        spinlock_acquire(&global_cached_pages_lock);
        FOREACH_LLIST_ENTRY (entry, &global_cached_pages, this_global) {
            if (!mmu_is_dirty((vaddr_t)entry->buffer))
                continue;
            block_device_cache_writeback(entry);
            mmu_clear_dirty((vaddr_t)entry->buffer);
        }
        spinlock_release(&global_cached_pages_lock);

        timer_wait_ms(1000);
    }
}

error_t block_device_cache_probe(void)
{
    struct thread *kthread;

    INIT_LLIST(global_cached_pages);
    INIT_SPINLOCK(global_cached_pages_lock);

    kthread = kthread_spawn(block_device_cache_flush, NULL);
    if (IS_ERR(kthread)) {
        PANIC("failed to create kernel thread for page cache");
    }

    sched_new_thread(kthread);

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, block_device_cache_probe);
