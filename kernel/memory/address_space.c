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

struct address_space kernel_address_space = {
    .mmu = 0, // Initialized when enabling paging
    .vmm = &kernel_vmm,
    .segments = NULL,
};

static inline struct vm_segment *to_segment(const node_t *this)
{
    return container_of(this, struct vm_segment, this);
}

/* We should not be modifying another process's address_space */
#define VM_CHECK_AS(_as) \
    WARN_ON((_as) != &kernel_address_space && (_as) != current->process->as);

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

    LLIST_INIT(as->segments);
    LLIST_INIT(as->kmalloc);

    return as;

vm_new_nomem:
    kfree(as->vmm);
    kfree(as);
    return PTR_ERR(E_NOMEM);
}

error_t address_space_init(struct address_space *as)
{
    bool success;

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

    FOREACH_LLIST_SAFE(this, node, as->segments)
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
    // Avoid being rescheduled
    const bool interrupts = scheduler_lock();

    current->process->as = as;
    mmu_load(as->mmu);
    thread_set_mmu(current, as->mmu);

    scheduler_unlock(interrupts);

    return E_SUCCESS;
}

error_t address_space_copy_current(struct address_space *dst)
{
    // The destination address space should be emptied before copying into it.
    // This is to avoid having 'zombie' segments left inaccessible after.
    if (!llist_is_empty(dst->segments) || dst->kmalloc)
        return E_BUSY;

    mmu_clone(dst->mmu);

    return E_NOT_IMPLEMENTED;
}

void *vm_alloc_start(struct address_space *as, void *addr, size_t size,
                     vm_flags flags)
{
    struct vm_segment *segment;

    if (size % PAGE_SIZE)
        return PTR_ERR(E_INVAL);

    segment = vmm_allocate(as->vmm, (vaddr_t)addr, size, flags);
    if (IS_ERR(segment))
        return segment;

    segment->flags = flags;

    return (void *)segment->start;
}

void *vm_alloc(struct address_space *as, size_t size, vm_flags flags)
{
    return vm_alloc_start(as, 0, size, flags);
}

void *
vm_alloc_at(struct address_space *as, paddr_t phys, size_t size, vm_flags flags)
{
    struct vm_segment *segment;

    if (size % PAGE_SIZE)
        return PTR_ERR(E_INVAL);

    if (phys % PAGE_SIZE)
        return PTR_ERR(E_INVAL);

    segment = vmm_allocate(as->vmm, 0, size, flags);
    if (IS_ERR(segment))
        return segment;

    /* In case the caller did not get this physical address through the page
     * allocator, we should mark these pages as currently in use. */
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        page_get(address_to_page(phys + off));

    VM_CHECK_AS(as);
    if (!mmu_map_range(segment->start, phys, size, flags))
        goto vm_allocate_release;

    segment->flags = flags;

    return (void *)segment->start;

vm_allocate_release:
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        page_put(address_to_page(phys + off));

    vmm_free(as->vmm, segment->start, segment->size);
    return NULL;
}

void vm_free(struct address_space *as, void *addr, size_t size)
{
    paddr_t phys;

    if (addr == NULL)
        return;

    if ((vaddr_t)addr % PAGE_SIZE) {
        log_warn("freeing unaligned virtual address: %p (skipping)", addr);
        return;
    }

    if (size % PAGE_SIZE) {
        log_warn("freeing unaligned number of bytes: %ld (skipping)", size);
        return;
    }

    VM_CHECK_AS(as);

    for (size_t off = 0; off < size; off += PAGE_SIZE) {
        phys = mmu_unmap((vaddr_t)addr + off);
        if (phys != PMM_INVALID_PAGEFRAME)
            pmm_free(phys);
    }

    vmm_free(as->vmm, (vaddr_t)addr, size);
}

struct vm_segment *vm_find(const struct address_space *as, void *addr)
{
    return vmm_find(as->vmm, (vaddr_t)addr);
}

static error_t vm_segment_map(struct address_space *as, struct vm_segment *segment)
{
    size_t off;
    paddr_t phys;

    VM_CHECK_AS(as);

    for (off = 0; off < segment->size; off += PAGE_SIZE) {
        phys = pmm_allocate();
        if (phys == PMM_INVALID_PAGEFRAME)
            goto err_release_allocated;
        if (!mmu_map(segment->start + off, phys, segment->flags)) {
            pmm_free(phys);
            goto err_release_allocated;
        }
    }

    if (segment->flags & VM_CLEAR)
        memset((void *)segment->start, 0, segment->size);

    return E_SUCCESS;

err_release_allocated:
    for (size_t off_release = 0; off_release < off; off_release += PAGE_SIZE) {
        phys = mmu_unmap(segment->start + off_release);
        pmm_free(phys);
    }
    return E_NOMEM;
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
    llist_remove(&as->segments, &segment->this);
}

error_t address_space_fault(struct address_space *as, void *addr, bool is_cow)
{
    struct vm_segment *segment = vm_find(as, addr);
    error_t ret;

    if (!segment)
        return -E_NOENT;

    // Copy-on write pages
    if (is_cow) {
        mmu_copy_on_write((vaddr_t)addr);
        return E_SUCCESS;
    }

    // Lazily allocate pageframes
    ret = vm_segment_map(as, segment);
    if (ret) {
        log_warn("failed to map segment @ " FMT32, segment->start);
        vm_free(as, (void *)segment->start, segment->size);
    }

    return E_SUCCESS;
}
