#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/vm.h>
#include <kernel/vmm.h>

#include <string.h>

struct vm_segment *vm_vnode_alloc(struct address_space *as, vaddr_t addr,
                                  size_t size, vm_flags_t flags)
{
    return vmm_allocate(as->vmm, addr, size, flags);
}

struct vm_segment *vm_vnode_alloc_at(struct address_space *as, paddr_t phys,
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

static void vm_vnode_free(struct address_space *as, struct vm_segment *segment)
{
    size_t size = segment->size;
    paddr_t phys;

    if (!IS_KERNEL_ADDRESS(segment->start))
        AS_ASSERT_OWNED(as);

    for (size_t off = 0; off < size; off += PAGE_SIZE) {
        phys = mmu_unmap(segment->start + off);
        if (phys != PMM_INVALID_PAGEFRAME)
            pmm_free(phys);
    }

    vmm_free(as->vmm, segment->start, size);
}

static error_t
vm_vnode_fault(struct address_space *as, struct vm_segment *segment)
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
        /*
         * Part of the segment may already have been mapped in case the faulty
         * address is one resulting from a segment resizing.
         */
        if (mmu_is_mapped(segment->start + off))
            continue;
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

    log_err("failed to map segment @ " FMT32 ": %pe", segment->start, &err);
    vm_vnode_free(as, segment);

    return err;
}

static error_t vm_vnode_resize(struct address_space *as,
                               struct vm_segment *segment, size_t new_size)
{
    vaddr_t old_end = segment_end(segment);
    paddr_t phys;
    error_t ret;

    AS_ASSERT_OWNED(as);

    ret = vmm_resize(as->vmm, to_vma(segment), new_size);
    if (ret != E_SUCCESS)
        return ret;

    /*
     * If the segment's size has been reduced free the allocated memory.
     */
    if (segment_end(segment) < old_end) {
        for (vaddr_t page = segment_end(segment); page < old_end;
             page += PAGE_SIZE) {
            phys = mmu_unmap(page);
            if (phys != PMM_INVALID_PAGEFRAME)
                pmm_free(phys);
        }
    }

    return E_SUCCESS;
}

const struct vm_segment_driver vm_vnode = {
    .vm_alloc = vm_vnode_alloc,
    .vm_alloc_at = vm_vnode_alloc_at,
    .vm_free = vm_vnode_free,
    .vm_fault = vm_vnode_fault,
    .vm_resize = vm_vnode_resize,
};
