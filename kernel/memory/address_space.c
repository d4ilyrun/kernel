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

struct address_space kernel_address_space = {
    .mmu = 0, // Initialized when enabling paging
    .vmm = &kernel_vmm,
    .segments = LLIST_INIT(kernel_address_space.segments),
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

    return as;

vm_new_nomem:
    kfree(as->vmm);
    kfree(as);
    return PTR_ERR(E_NOMEM);
}

error_t address_space_init(struct address_space *as)
{
    bool success;

    INIT_LLIST(as->segments);
    INIT_LLIST(as->kmalloc);

    if (as == &kernel_address_space)
        success = vmm_init(as->vmm, KERNEL_MEMORY_START, KERNEL_MEMORY_END);
    else
        success = vmm_init(as->vmm, USER_MEMORY_START, USER_MEMORY_END);

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

    FOREACH_LLIST_SAFE(this, node, &as->segments)
    {
        segment = to_segment(this);
        for (size_t off = 0; off < segment->size; off += PAGE_SIZE) {
            phys = mmu_unmap(segment->start + off);
            if (phys != PMM_INVALID_PAGEFRAME)
                pmm_free(phys);
        }
    }

    vmm_clear(as->vmm);

    return E_SUCCESS;
}

error_t address_space_destroy(struct address_space *as)
{
    // We don't want to destroy the MMU while it is still being used.
    if (as != current->process->as) {
        log_err("Trying to destroy kernel address space");
        return E_INVAL;
    }

    vmm_destroy(as->vmm);
    mmu_destroy(as->mmu);
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
    // The destination address space should be emptied before copying into it.
    // This is to avoid having 'zombie' segments left inaccessible after.
    if (!llist_is_empty(&dst->segments) || !llist_is_empty(&dst->kmalloc))
        return E_BUSY;

    no_preemption_scope () {
        mmu_clone(dst->mmu); /* Clone current MMU into the destination AS */
        vmm_copy(dst->vmm, current->process->as->vmm);
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

    segment = driver->vm_alloc(as, (vaddr_t)addr, size, flags);
    if (IS_ERR(segment))
        return NULL;

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

    segment = driver->vm_alloc_at(as, phys, size, flags);
    if (IS_ERR(segment))
        return NULL;

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

    segment = vm_find(as, addr);
    if (!segment) {
        log_dbg("free: no backing segment for %p", addr);
        return;
    }

    llist_remove(&segment->this);
    segment->driver->vm_free(as, segment);
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
    node_t *segment = llist_find_first(&as->segments, addr, vm_segment_contains);

    return segment ? to_segment(segment) : NULL;
}

static int vm_segment_compare(const void *left, const void *right)
{
    RETURN_CMP(to_segment(left)->start, to_segment(right)->start);
}

void vm_segment_insert(struct address_space *as, struct vm_segment *segment)
{
    llist_insert_sorted(&as->segments, &segment->this, vm_segment_compare);
}

void vm_segment_remove(struct address_space *as, struct vm_segment *segment)
{
    UNUSED(as);
    llist_remove(&segment->this);
}
