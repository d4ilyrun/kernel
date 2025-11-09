/*
 * SunOs' slab allocator implementation.
 *
 * TODO: kmem_cache_reap() for reclaiming memory from caches when running low.
 *
 * References:
 * - Bonwick94
 */

#define LOG_DOMAIN "slab"

#include <kernel/error.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/memory/slab.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/spinlock.h>
#include <kernel/vm.h>

#include <libalgo/hashtable.h>

enum kmem_cache_flag {
    CACHE_F_EXTERNAL, /* slab & bufctl structs stored in an external buffer. */
};

/*
 *
 */
struct kmem_bufctl {
    union {
        struct kmem_bufctl *next; /* Used when bufctl is inside the freelist. */
        struct kmem_slab *slab;   /* Used when the object has been allocated. */
    };

    /*
     * NOTE: The object and the hash table entry are placed inside a union
     *       so that the object and the entry's key can be used interchangeably.
     */
    union {
        void *obj;
        struct hashtable_entry hash;
    };
};

static struct kmem_cache kmem_cache_cache;
static struct kmem_cache kmem_slab_cache;
static struct kmem_cache kmem_bufctl_cache;

#define KMEM_SLAB_MIN_SIZE sizeof(struct kmem_bufctl)
#define KMEM_SLAB_MIN_ALIGN 1

/*
 * Slabs whose objects are larger than this one are considered 'large slabs'.
 *
 * Large slabs do not store the kmem_bufctl structures inside the slab directly,
 * but keep them stored inside a dedicated buffer.
 */
#define KMEM_SLAB_LARGE_SIZE (PAGE_SIZE / 8)

/*
 * Address to kmem_bufctl hashmap.
 */
static DECLARE_HASHTABLE(kmem_bufctl_hashmap, 256);
static DECLARE_SPINLOCK(kmem_bufctl_hashmap_lock);

/*
 * NOTE: Using a spinlock to protect against simultaneous accesses to the
 *       slab lists makes it unsafe to use the kmem_cache API in uninterruptible
 *       contexts. We should switch to disabling interrupts instead if we need
 *       to allocate memory while inside an interrupt handler.
 */
static inline void kmem_cache_lock(struct kmem_cache *cache)
{
    spinlock_acquire(&cache->lock);
}

/*
 *
 */
static inline void kmem_cache_unlock(struct kmem_cache *cache)
{
    spinlock_release(&cache->lock);
}

/*
 * Find the start address of a slab object.
 */
static inline void *kmem_slab_obj_start(const struct kmem_slab *slab, void *obj)
{
    off_t offset = obj - slab->page;

    return obj - (offset % slab->cache->obj_real_size);
}

/*
 * Find the start address of the next object in a slab.
 *
 * This function assumes that @c obj points to the start of the object.
 */
static inline void *kmem_slab_obj_next(const struct kmem_slab *slab, void *obj)
{
    return obj + slab->cache->obj_real_size;
}

/*
 * Check whether @c has been allocated by @c slab.
 */
static inline bool
kmem_slab_contains_obj(const struct kmem_slab *slab, void *obj)
{
    return PAGE_ALIGN_DOWN(obj) == slab->page;
}

/*
 * Free a single slab and all the bufctl structure it contains.
 */
static void kmem_slab_destroy(struct kmem_slab *slab)
{
    struct kmem_cache *cache = slab->cache;
    struct kmem_bufctl *bufctl;
    struct kmem_bufctl *next;

    if (atomic_read(&slab->refcount)) {
        log_warn("%s: slab@%p has %d active entries when destroying",
                 cache->name, slab->page, atomic_read(&slab->refcount));
    }

    llist_remove(&slab->this);
    vm_free(&kernel_address_space, slab->page);

    next = slab->free;
    while ((bufctl = next) != NULL) {
        next = bufctl->next;
        locked_scope (&kmem_bufctl_hashmap_lock) {
            hashtable_remove(&kmem_bufctl_hashmap, &bufctl->hash.key);
        }
        if (cache->destructor)
            cache->destructor(bufctl->obj);
        if (BIT_READ(cache->flags, CACHE_F_EXTERNAL))
            kmem_cache_free(&kmem_bufctl_cache, bufctl);
    }

    kmem_cache_free(&kmem_slab_cache, slab);
}

/*
 *
 */
static void
kmem_slab_free_obj(struct kmem_slab *slab, struct kmem_bufctl *bufctl)
{
    bufctl->next = slab->free;
    SWAP(slab->free, bufctl);

    if (unlikely(atomic_dec(&slab->refcount) <= 1)) {
        /* All objects in the slab are now free. */
        llist_remove(&slab->this);
        llist_add(&slab->cache->slabs_free, &slab->this);
    } else if (unlikely(bufctl == NULL)) {
        /* Slab was full before freeing the object. */
        llist_remove(&slab->this);
        llist_add(&slab->cache->slabs_partial, &slab->this);
    }
}

/*
 * Generate the slab's initial freelist and initialize each object.
 *
 * This function only works for normal sized objects. For larger objects,
 * use kmem_slab_init_large_objects().
 */
static void kmem_slab_init_objects(struct kmem_slab *slab)
{
    struct kmem_cache *cache = slab->cache;
    struct kmem_bufctl *bufctl;
    struct kmem_bufctl **next_bufctl;
    void *end = slab;
    void *obj;

    obj = slab->page + slab->coloring_offset;
    next_bufctl = &slab->free;

    while (obj + cache->obj_real_size <= end) {
        bufctl = obj + cache->obj_size;
        *next_bufctl = bufctl;
        next_bufctl = &bufctl->next;
        bufctl->obj = obj;

        locked_scope (&kmem_bufctl_hashmap_lock) {
            bufctl->hash.key = bufctl->obj;
            hashtable_insert(&kmem_bufctl_hashmap, &bufctl->hash);
        }

        if (cache->constructor)
            cache->constructor(obj);
        obj += cache->obj_real_size;
    }

    *next_bufctl = NULL;
}

/*
 * Generate the slab's initial freelist and initiliaze each object.
 *
 * This is the large object version of @ref kmem_slab_init_objects() for slabs
 * in which bufctl structures are stored in a dedicated external page.
 */
static error_t kmem_slab_init_large_objects(struct kmem_slab *slab)
{
    struct kmem_cache *cache = slab->cache;
    size_t slab_size = align_up(cache->obj_size, PAGE_SIZE);
    struct kmem_bufctl *bufctl;
    struct kmem_bufctl **next_bufctl;
    void *end = slab->page + slab_size;
    void *obj;

    obj = slab->page + slab->coloring_offset;
    next_bufctl = &slab->free;

    while (obj + cache->obj_real_size <= end) {
        bufctl = kmem_cache_alloc(&kmem_bufctl_cache, 0);
        if (!bufctl) {
            *next_bufctl = NULL;
            goto err;
        }

        *next_bufctl = bufctl;
        next_bufctl = &bufctl->next;
        bufctl->obj = obj;

        locked_scope (&kmem_bufctl_hashmap_lock) {
            bufctl->hash.key = bufctl->obj;
            hashtable_insert(&kmem_bufctl_hashmap, &bufctl->hash);
        }

        if (cache->constructor)
            cache->constructor(obj);
        obj += cache->obj_real_size;
    }

    *next_bufctl = NULL;

    return 0;

err:
    bufctl = slab->free;
    while (bufctl) {
        struct kmem_bufctl *to_free = bufctl;
        bufctl = bufctl->next;
        kmem_cache_free(&kmem_bufctl_cache, to_free);
    }

    return -1;
}

/*
 * Allocate, construct and add a new slab to the cache.
 *
 * The frontend slab API should call this function when all slabs are full when
 * allocating.
 *
 * This function must be called with the cache's lock held.
 *
 * @return The added slab, or an pointer encoded error.
 */
static struct kmem_slab *kmem_cache_grow(struct kmem_cache *cache, int flags)
{
    struct kmem_slab *slab;
    void *page;
    paddr_t paddr;
    size_t slab_size;
    unsigned int max_color_offset;

    UNUSED(flags);

    slab_size = align_up(cache->obj_size, PAGE_SIZE);

    page = vm_alloc(&kernel_address_space, slab_size, VM_KERNEL_RW);
    if (!page)
        return PTR_ERR(E_NOMEM);

    if (BIT_READ(cache->flags, CACHE_F_EXTERNAL)) {
        max_color_offset = slab_size % cache->obj_real_size;
        slab = kmem_cache_alloc(&kmem_slab_cache, 0);
        if (!slab) {
            slab = PTR_ERR(E_NOMEM);
            goto err;
        }
    } else {
        /* The slab struct is placed the end of the slab */
        slab = page + slab_size - sizeof(*slab);
        max_color_offset = (slab_size - sizeof(*slab)) % cache->obj_real_size;
    }

    slab->page = page;
    slab->cache = cache;

    /*
     * Set this slab's cache coloring offset and update the kmem_cache
     * offset for the next allocated slab.
     */
    slab->coloring_offset = cache->coloring_offset_next;
    cache->coloring_offset_next += cache->obj_align;
    if (cache->coloring_offset_next > max_color_offset)
        cache->coloring_offset_next = 0;

    if (BIT_READ(cache->flags, CACHE_F_EXTERNAL)) {
        /* The page is not guaranteed to be accessed in this case, but we need
         * a physical page to initialize the page structure later. */
        vm_map(&kernel_address_space, page);
        if (kmem_slab_init_large_objects(slab))
            goto err;
    } else {
        kmem_slab_init_objects(slab);
    }

    paddr = mmu_find_physical((vaddr_t)page);
    for (size_t off = 0; off < slab_size; off += PAGE_SIZE) {
        address_to_page(paddr + off)->flags |= PAGE_SLAB;
        address_to_page(paddr + off)->slab.cache = cache;
    }

    atomic_write(&slab->refcount, 0);
    llist_add_tail(&cache->slabs_free, &slab->this);

    return slab;

err:
    vm_free(&kernel_address_space, page);
    return slab;
}

/*
 *
 */
void *kmem_cache_alloc(struct kmem_cache *cache, int flags)
{
    struct kmem_slab *slab;
    struct kmem_bufctl *bufctl;
    llist_t *slabs;
    void *obj = NULL;

    kmem_cache_lock(cache);

    /*
     * Find a slab with at least one free object, and if none are present
     * allocate one.
     */
    if (!llist_is_empty(&cache->slabs_partial)) {
        slabs = &cache->slabs_partial;
        slab = container_of(llist_first(slabs), struct kmem_slab, this);
    } else {
        slabs = &cache->slabs_free;
        if (!llist_is_empty(&cache->slabs_free)) {
            slab = container_of(llist_first(slabs), struct kmem_slab, this);
        } else {
            slab = kmem_cache_grow(cache, flags);
            if (IS_ERR(slab))
                goto out;
        }
    }

    /* Pop the free object from the slab's freelist. */
    bufctl = slab->free;
    atomic_inc(&slab->refcount);
    slab->free = bufctl->next;

    /* Make the now active bufctl point to its slab. */
    bufctl->slab = slab;
    obj = bufctl->obj;

    if (slab->free == NULL) {
        llist_remove(&slab->this);
        llist_add(&cache->slabs_full, &slab->this);
    } else if (slabs == &cache->slabs_free) {
        llist_remove(&slab->this);
        llist_add(&cache->slabs_partial, &slab->this);
    }

out:
    kmem_cache_unlock(cache);
    return obj;
}

/*
 *
 */
void kmem_cache_free(struct kmem_cache *cache, void *obj)
{
    struct kmem_slab *slab;
    struct kmem_bufctl *bufctl;
    struct hashtable_entry *hash_entry;
    struct page *page;
    paddr_t paddr;

    UNUSED(cache);

    /*
     * Slab pages always have a backing physical page since we access them
     * to create the freelist in kmem_cache_grow(). If this is not verified
     * it either means this page is not a slab page or that something went
     * wrong somewhere.
     */
    paddr = mmu_find_physical((vaddr_t)obj);
    if (WARN_ON_MSG(IS_ERR(paddr), "free: no backing page for object at %p",
                    obj)) {
        return;
    }

    page = address_to_page(paddr);
    if (WARN_ON_MSG(!(page->flags & PAGE_SLAB),
                    "free: object at %p is not a slab object (flags: %04x)",
                    obj, page->flags)) {
        return;
    }

    locked_scope (&kmem_bufctl_hashmap_lock) {
        obj = align_down_ptr(obj, cache->obj_align);
        hash_entry = hashtable_find(&kmem_bufctl_hashmap, obj);
        if (WARN_ON_MSG(!hash_entry, "free: no bufctl found for %p", obj))
            return;
    }

    bufctl = container_of(hash_entry, struct kmem_bufctl, hash);
    slab = bufctl->slab;
    if (WARN_ON(slab->cache != cache))
        return;

    kmem_cache_lock(cache);
    kmem_slab_free_obj(slab, bufctl);
    kmem_cache_unlock(cache);
}

/*
 * Initialize a kmem_cache structure.
 */
static void kmem_cache_init(struct kmem_cache *cache, const char *name,
                            size_t obj_size, int obj_align,
                            void (*constructor)(void *),
                            void (*destructor)(void *))
{
    cache->name = name;
    cache->obj_size = obj_size;
    cache->obj_align = obj_align;
    cache->constructor = constructor;
    cache->destructor = destructor;

    cache->coloring_offset_next = 0;
    cache->flags = 0;

    /*
     * Slabs and bufctl struct for large objects are stored inside a dedicated
     * external buffer. Regular slabs append the kmem_bufctl struct after
     * the object directly inside the slab.
     */
    if (obj_size >= KMEM_SLAB_LARGE_SIZE) {
        BIT_SET(cache->flags, CACHE_F_EXTERNAL);
        cache->obj_real_size = align_up(obj_size, obj_align);
    } else {
        cache->obj_real_size = align_up(obj_size + sizeof(struct kmem_bufctl),
                                        obj_align);
    }
}

/*
 *
 */
struct kmem_cache *kmem_cache_create(const char *name, size_t obj_size,
                                     int obj_align, void (*constructor)(void *),
                                     void (*destructor)(void *))
{
    struct kmem_cache *cache;

    if (obj_size < KMEM_SLAB_MIN_SIZE)
        return PTR_ERR(E_INVAL);

    if (obj_align < KMEM_SLAB_MIN_ALIGN || !is_power_of_2(obj_align))
        return PTR_ERR(E_INVAL);

    cache = kmem_cache_alloc(&kmem_cache_cache, 0);
    if (!cache) {
        log_err("failed to allocate %s cache", name);
        return PTR_ERR(E_NOMEM);
    }

    kmem_cache_init(cache, name, obj_size, obj_align, constructor, destructor);

    return cache;
}

/*
 *
 */
void kmem_cache_destroy(struct kmem_cache *cache)
{
    struct kmem_slab *slab;
    struct kmem_slab *next;

    FOREACH_LLIST_ENTRY_SAFE (slab, next, &cache->slabs_partial, this) {
        kmem_slab_destroy(slab);
    }

    FOREACH_LLIST_ENTRY_SAFE (slab, next, &cache->slabs_full, this) {
        kmem_slab_destroy(slab);
    }

    FOREACH_LLIST_ENTRY_SAFE (slab, next, &cache->slabs_free, this) {
        kmem_slab_destroy(slab);
    }
}

/*
 * Constructor for kmem_cache structures.
 */
static void kmem_cache_constructor(void *data)
{
    struct kmem_cache *cache = data;

    INIT_LLIST(cache->slabs_full);
    INIT_LLIST(cache->slabs_partial);
    INIT_LLIST(cache->slabs_free);
    INIT_SPINLOCK(cache->lock);
}

/*
 * Destructor for kmem_cache structures.
 */
static void kmem_cache_destructor(void *data)
{
    UNUSED(data);
}

/*
 *
 */
int kmem_cache_api_init(void)
{
    /*
     * Larger slabs require a separate hashtable, wihch we would need to
     * dynamically allocate entries for. This is not feasible for the bootstrap
     * cache of kmem_cache structures.
     */
    static_assert(sizeof(struct kmem_cache) < KMEM_SLAB_LARGE_SIZE);

    hashtable_init(&kmem_bufctl_hashmap);
    INIT_SPINLOCK(kmem_bufctl_hashmap_lock);

    kmem_cache_constructor(&kmem_cache_cache);
    kmem_cache_init(&kmem_cache_cache, "kmem_cache", sizeof(struct kmem_cache),
                    1, kmem_cache_constructor, kmem_cache_destructor);

    kmem_cache_constructor(&kmem_slab_cache);
    kmem_cache_init(&kmem_slab_cache, "kmem_slab", sizeof(struct kmem_slab), 1,
                    NULL, NULL);

    kmem_cache_constructor(&kmem_bufctl_cache);
    kmem_cache_init(&kmem_bufctl_cache, "kmem_bufctl",
                    sizeof(struct kmem_bufctl), 1, NULL, NULL);

    return 0;
}
