#define LOG_DOMAIN "vm"

#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/vm.h>
#include <kernel/vmm.h>

#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

/*
 * We don't want to expose the MMU API everywhere we include the VM header,
 * but we NEED to be sure that we can cast the VM_* flags to their MMU_PROT
 * equivalent.
 */

static_assert((int)VM_EXEC == (int)PROT_EXEC);
static_assert((int)VM_READ == (int)PROT_READ);
static_assert((int)VM_WRITE == (int)PROT_WRITE);
static_assert((int)VM_KERNEL == (int)PROT_KERNEL);

static DECLARE_LLIST(kernel_kmalloc);
static DECLARE_LLIST(kernel_segments);

struct address_space kernel_address_space = {
    .mmu = 0, // Initialized when enabling paging
    .vmm = &kernel_vmm,
    .segments = &kernel_segments,
    .kmalloc = &kernel_kmalloc,
    .lock = SPINLOCK_INIT,
};

/** Data used to match a vm_alloc request with the appropriate driver.
 *
 *  Drivers are matched using the request's flags.
 */
struct vm_segment_driver_match {
    const struct vm_segment_driver *driver;
    vm_flags_t flags;
};

extern const struct vm_segment_driver vm_normal;

static struct vm_segment_driver_match vm_segment_drivers[] = {
    {.driver = &vm_normal, .flags = 0},
};

static inline struct vm_segment *to_segment(const node_t *this)
{
    return container_of(this, struct vm_segment, this);
}

static inline int vm_segment_compare(const void *left, const void *right)
{
    RETURN_CMP(to_segment(left)->start, to_segment(right)->start);
}

static inline void
vm_segment_insert(struct address_space *as, struct vm_segment *segment)
{
    llist_insert_sorted(as->segments, &segment->this, vm_segment_compare);
}

static inline void
vm_segment_remove(struct address_space *as, struct vm_segment *segment)
{
    UNUSED(as);
    llist_remove(&segment->this);
}

static const struct vm_segment_driver *vm_find_driver(vm_flags_t flags)
{
    struct vm_segment_driver_match *match;

    for (size_t i = 0; i < ARRAY_SIZE(vm_segment_drivers); ++i) {
        match = &vm_segment_drivers[i];
        if ((flags & match->flags) == match->flags)
            return match->driver;
    }

    /* If no match has been found, vm_normal is always the default choice. */
    return &vm_normal;
}

struct address_space *address_space_new(void)
{
    struct address_space *as;

    as = kcalloc(1, sizeof(*as), KMALLOC_KERNEL);
    if (as == NULL)
        return PTR_ERR(E_NOMEM);

    as->vmm = vmm_new(as);
    if (as->vmm == NULL)
        goto vm_new_nomem;

    as->mmu = mmu_new();
    if (as->mmu == PMM_INVALID_PAGEFRAME)
        goto vm_new_nomem;

    INIT_SPINLOCK(as->lock);

    return as;

vm_new_nomem:
    kfree(as->vmm);
    kfree(as);
    return PTR_ERR(E_NOMEM);
}

error_t address_space_init(struct address_space *as)
{
    struct vm_segment *segment;
    llist_t *list_head;
    paddr_t page;
    bool success;

    if (as == &kernel_address_space) {
        success = vmm_init(as->vmm, KERNEL_MEMORY_START, KERNEL_MEMORY_END);
        return success ? E_SUCCESS : E_INVAL;
    }

    success = vmm_init(as->vmm, USER_MEMORY_START, USER_MEMORY_END);

    /*
     * Allocate the heads of the address space's lists.
     *
     * We need to dynamically allocate them so that they also get duplicated
     * through coW. otherwise, we'd have to edit clone the whole list and update
     * all next/prev pointers accordingly.
     */

    segment = vm_normal.vm_alloc(as, 0, sizeof(*list_head), VM_READ | VM_WRITE);
    if (IS_ERR(segment))
        return ERR_FROM_PTR(segment);

    /*
     * Since we cannot recursively keep track of the segment list head inside
     * the segment list, we use segment we just allocated as its sentinel.
     */
    as->segments = (llist_t *)&segment->this;
    as->kmalloc = (void *)segment->start;

    /*
     * We cannot lazily allocate the kmalloc list segment since the pagefault
     * handler will not be able to retrive it and will see it as an
     * actual faulty access.
     */
    page = pmm_allocate();
    if (page == PMM_INVALID_PAGEFRAME)
        return E_NOMEM;
    if (!mmu_map(segment->start, page, PROT_WRITE | PROT_READ))
        return E_EXIST;

    INIT_LLIST(*as->segments);
    INIT_LLIST(*as->kmalloc);

    return success ? E_SUCCESS : E_INVAL;
}

error_t address_space_clear(struct address_space *as)
{
    struct vm_segment *segment;
    paddr_t phys;

    // We need to be able to read the address space's MMU to retreive
    // The physical addresses associated with segments.
    WARN_ON(as != current->process->as);
    WARN_ON(as == &kernel_address_space);
    WARN_ON(as == kernel_process.as);

    locked_scope (&as->lock) {

        FOREACH_LLIST_SAFE (this, node, as->segments) {
            segment = to_segment(this);
            for (size_t off = 0; off < segment->size; off += PAGE_SIZE) {
                phys = mmu_unmap(segment->start + off);
                if (phys != PMM_INVALID_PAGEFRAME)
                    pmm_free(phys);
            }
        }

        /* The list's sentinel was also dynamically allocated. */
        segment = to_segment(llist_head(as->segments));
        for (size_t off = 0; off < segment->size; off += PAGE_SIZE) {
            phys = mmu_unmap(segment->start + off);
            if (phys != PMM_INVALID_PAGEFRAME)
                pmm_free(phys);
        }

        as->data_end = 0;
        as->brk_end = as->data_end;
        as->segments = NULL;
        as->kmalloc = NULL;

        vmm_clear(as->vmm);
    }

    return E_SUCCESS;
}

error_t address_space_destroy(struct address_space *as)
{
    // We don't want to destroy the MMU while it is still being used.
    if (as != current->process->as) {
        log_err("Trying to destroy kernel address space");
        return E_INVAL;
    }

    locked_scope (&as->lock) {
        vmm_destroy(as->vmm);
        mmu_destroy(as->mmu);
    }

    return E_SUCCESS;
}

error_t address_space_load(struct address_space *as)
{
    no_preemption_scope () {
        current->process->as = as;
        mmu_load(as->mmu);
        thread_set_mmu(current, as->mmu);
    }

    return E_SUCCESS;
}

error_t address_space_copy_current(struct address_space *dst)
{
    struct address_space *src = current->process->as;

    /*
     * The destination address space should be cleared before copying into it.
     * This is to avoid having 'zombie' segments left inaccessible after.
     */
    if (dst->segments)
        return E_BUSY;

    locked_scope (&dst->lock) {
        no_preemption_scope () {
            mmu_clone(dst->mmu); /* Clone current MMU into the destination AS */
            vmm_copy(dst->vmm, src->vmm);
        }

        dst->segments = src->segments;
        dst->kmalloc = src->kmalloc;
        dst->data_end = src->data_end;
        dst->brk_end = src->brk_end;
    }

    return E_SUCCESS;
}

error_t address_space_fault(struct address_space *as, void *addr, bool is_cow)
{
    struct vm_segment *segment = vm_find(as, addr);
    error_t err;

    if (!segment)
        return -E_NOENT;

    /*
     * Copy-on-Write pages
     */
    if (is_cow) {
        err = mmu_copy_on_write((vaddr_t)addr);
        if (err)
            log_warn("cow @ %p failed: %s", addr, err_to_str(err));
        return E_SUCCESS;
    }

    return segment->driver->vm_fault(as, segment);
}

void *vm_alloc_start(struct address_space *as, void *addr, size_t size,
                     vm_flags_t flags)
{
    const struct vm_segment_driver *driver;
    struct vm_segment *segment;

    if (size % PAGE_SIZE)
        return NULL;

    driver = vm_find_driver(flags);
    if (!driver)
        return NULL;

    locked_scope (&as->lock) {
        segment = driver->vm_alloc(as, (vaddr_t)addr, size, flags);
        if (IS_ERR(segment))
            return NULL;
        vm_segment_insert(as, segment);
    }

    segment->flags = flags;
    segment->driver = driver;

    return (void *)segment->start;
}

void *vm_alloc(struct address_space *as, size_t size, vm_flags_t flags)
{
    return vm_alloc_start(as, 0, size, flags);
}

void *vm_alloc_at(struct address_space *as, paddr_t phys, size_t size,
                  vm_flags_t flags)
{
    const struct vm_segment_driver *driver;
    struct vm_segment *segment;

    if (size % PAGE_SIZE)
        return NULL;

    if (phys % PAGE_SIZE)
        return NULL;

    driver = vm_find_driver(flags);
    if (!driver)
        return NULL;

    locked_scope (&as->lock) {
        segment = driver->vm_alloc_at(as, phys, size, flags);
        if (IS_ERR(segment))
            return NULL;
        vm_segment_insert(as, segment);
    }

    segment->flags = flags;
    segment->driver = driver;

    return (void *)segment->start;
}

void vm_free(struct address_space *as, void *addr)
{
    struct vm_segment *segment;

    if (addr == NULL)
        return;

    if ((vaddr_t)addr % PAGE_SIZE) {
        log_warn("freeing unaligned virtual address: %p (skipping)", addr);
        return;
    }

    locked_scope (&as->lock) {
        segment = vm_find(as, addr);
        if (!segment) {
            log_dbg("free: no backing segment for %p", addr);
            return;
        }

        vm_segment_remove(as, segment);
        segment->driver->vm_free(as, segment);
    }
}

static int vm_segment_contains(const void *this, const void *addr)
{
    const struct vm_segment *segment = to_segment(this);

    if (!IN_RANGE((vaddr_t)addr, segment->start, segment_end(segment) - 1))
        return !COMPARE_EQ;

    return COMPARE_EQ;
}

struct vm_segment *vm_find(const struct address_space *as, void *addr)
{
    node_t *segment = llist_find_first(as->segments, addr,
                                       vm_segment_contains);

    return segment ? to_segment(segment) : NULL;
}

error_t vm_resize_segment(struct address_space *as, struct vm_segment *segment,
                          size_t new_size)
{
    if (!PAGE_ALIGNED(new_size)) {
        log_err("resize: segment size must be page aligned");
        return E_INVAL;
    }

    if (new_size == segment->size)
        return E_SUCCESS;

    locked_scope(&as->lock) {
        if (new_size == 0) {
            segment->driver->vm_free(as, segment);
            return E_SUCCESS;
        }

        if (!segment->driver->vm_resize)
            return E_NOT_SUPPORTED;

        return segment->driver->vm_resize(as, segment, new_size);
    }

    assert_not_reached();
}

static void *vm_brk(struct address_space *as, vaddr_t new_end)
{
    void *curr_brk_end = (void *)as->brk_end;
    struct vm_segment *brk;
    size_t aligned_new_end;
    size_t new_size;

    if (new_end < as->data_end)
        return PTR_ERR(E_NOMEM);

    if (new_end == as->brk_end)
        return curr_brk_end;

    aligned_new_end = align_up(new_end, PAGE_SIZE);

    /*
     * If no break segment currently exists, we need to create it.
     */
    if (curr_brk_end == (void *)as->data_end) {
        curr_brk_end = vm_alloc_start(as, (void *)as->data_end,
                                      aligned_new_end - as->data_end,
                                      VM_USER_RW | VM_FIXED);
        if (!curr_brk_end) {
            log_err("failed to allocate break segment");
            return PTR_ERR(E_NOMEM);
        }
    } else {
        /*
         * Locate the segment inside which the last byte INSIDE the break area
         * resides. A break segment should always exist inside the address space
         * at this time.
         */
        brk = vm_find(as, curr_brk_end - 1);
        if (!brk) {
            log_err("break segment should exist but could not be found");
            return PTR_ERR(E_NOMEM);
        }

        new_size = aligned_new_end - brk->start;
        vm_resize_segment(as, brk, new_size);
    }

    as->brk_end = new_end;

    return curr_brk_end;
}

error_t sys_brk(void *addr)
{
    void *old_end = vm_brk(current->process->as, (vaddr_t)addr);
    return IS_ERR(old_end) ? ERR_FROM_PTR(old_end) : E_SUCCESS;
}

void *sys_sbrk(intptr_t increment)
{
    return vm_brk(current->process->as,
                  current->process->as->brk_end + increment);
}
