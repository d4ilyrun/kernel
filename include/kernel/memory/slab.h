#ifndef KERNEL_MEMORY_SLAB_H
#define KERNEL_MEMORY_SLAB_H

#include <kernel/atomic.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>
#include <utils/macro.h>

/*
 *
 */
struct kmem_cache {
    llist_t slabs_full;
    llist_t slabs_partial;
    llist_t slabs_free;
    spinlock_t lock;

    size_t obj_size;
    int obj_align;
    size_t obj_real_size;
    unsigned int coloring_offset_next;

    void (*constructor)(void *data);
    void (*destructor)(void *data);

    const char *name;
    int flags;
};

/*
 *
 */
struct kmem_slab {
    void *page;
    struct kmem_bufctl *free;
    struct kmem_cache *cache;
    atomic_t refcount;
    unsigned int coloring_offset;
    node_t this;
};

/** Create a new cache. */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int obj_align, void (*constructor)(void *),
                                     void (*destructor)(void *));

/** Allocate an object from a cache
 *
 * @param cache The cache
 * @param flags Combination of allocation flags
 */
void *kmem_cache_alloc(struct kmem_cache *cache, int flags);

/** Free a cache and all its slabs.
 *
 *  NOTE: The caller should be sure that no objects allocated from this cache
 *        are still being used when calling this function.
 */
void kmem_cache_destroy(struct kmem_cache *cache);

/** Free an object allocated by a cache.
 *
 * @param cache The cache the object was allocated from
 * @param obj The object to free
 */
void kmem_cache_free(struct kmem_cache *cache, void *obj);

int kmem_cache_api_init(void);

#endif /* KERNEL_MEMORY_SLAB_H */
