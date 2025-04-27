#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/vm.h>
#include <kernel/vmm.h>

#include <string.h>

struct vm_segment *vm_normal_alloc(struct address_space *as, vaddr_t addr,
                                   size_t size, vm_flags_t flags)
{
    return vmm_allocate(as->vmm, addr, size, flags);
}

struct vm_segment *vm_normal_alloc_at(struct address_space *as, paddr_t phys,
                                      size_t size, vm_flags_t flags)
{
    struct vm_segment *segment;
    error_t err;

    AS_ASSERT_OWNED(as);

    segment = vmm_allocate(as->vmm, 0, size, flags);
    if (IS_ERR(segment))
        return segment;

    /* In case the caller did not get this physical address through the page
     * allocator, we should mark these pages as currently in use. */
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        page_get(address_to_page(phys + off));

    err = E_EXIST;
    if (!mmu_map_range(segment->start, phys, size, flags))
        goto vm_allocate_release;

    return segment;

vm_allocate_release:
    for (size_t off = 0; off < size; off += PAGE_SIZE)
        page_put(address_to_page(phys + off));
    vmm_free(as->vmm, segment->start, segment->size);
    return PTR_ERR(err);
}

static void vm_normal_free(struct address_space *as, struct vm_segment *segment)
{
    size_t size = segment->size;
    paddr_t phys;

    AS_ASSERT_OWNED(as);

    for (size_t off = 0; off < size; off += PAGE_SIZE) {
        phys = mmu_unmap(segment->start + off);
        if (phys != PMM_INVALID_PAGEFRAME)
            pmm_free(phys);
    }

    vmm_free(as->vmm, segment->start, size);
}

static error_t
vm_normal_fault(struct address_space *as, struct vm_segment *segment)
{
    paddr_t phys;
    size_t off;
    error_t err;

    AS_ASSERT_OWNED(as);

    /*
     * Perform lazy allocation of physical pages.
     */
    err = E_NOMEM;
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
    /*
     * Release the virtual addresses that were sucessfully mapped.
     */
    for (size_t off_release = 0; off_release < off; off_release += PAGE_SIZE) {
        phys = mmu_unmap(segment->start + off_release);
        pmm_free(phys);
    }

    log_err("failed to map segment @ " FMT32 ": %s", segment->start,
            err_to_str(err));
    vm_normal_free(as, segment);

    return err;
}

const struct vm_segment_driver vm_normal = {
    .vm_alloc = vm_normal_alloc,
    .vm_alloc_at = vm_normal_alloc_at,
    .vm_free = vm_normal_free,
    .vm_fault = vm_normal_fault,
};
