#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/vmm.h>

#include <libalgo/avl.h>
#include <libalgo/bitmap.h>
#include <utils/bits.h>
#include <utils/container_of.h>
#include <utils/macro.h>
#include <utils/math.h>

#include <string.h>

/* For simplicity, we will allocate 64B for each VMA structure */
#define VMA_SIZE (64)
static_assert(sizeof(vma_t) <= VMA_SIZE,
              "Update the allocated size for VMA structures!");

static DEFINE_INTERRUPT_HANDLER(page_fault);

/**
 * @defgroup vmm_internals VMM Internals
 * @ingroup VMM
 *
 * Internal functions and variables used by the VMM
 *
 * These are just used by ur implementation and should not be accessed by any
 * other part of the kernel
 */

vmm_t kernel_vmm;

/** Check whether a virtual address has been allocated using @ref kernel_vmm
 *  @ingroup vmm_internals
 */
#define IS_KERNEL_ADDRESS(_addr) \
    IN_RANGE((vaddr_t)(_addr), KERNEL_MEMORY_START, KERNEL_MEMORY_END)

/** Compute the end address of a VMA.
 * @ingroup vmm_internals
 */
static ALWAYS_INLINE vaddr_t vma_end(const vma_t *vma)
{
    return vma->start + vma->size;
}

/**
 * @brief Allocate memory for a single VMA from within the VMM's reserved area
 * @ingroup vmm_internals
 */
static vaddr_t vma_reserved_allocate(vmm_t *vmm)
{
    vaddr_t offset = -1;
    bool page_already_allocated = true;

    // Find the first available virtual address within the reserved range
    for (unsigned int i = 0; i < ARRAY_SIZE(vmm->reserved); ++i) {
        if (vmm->reserved[i] != (bitmap_block_t)-1) {
            offset = VMA_SIZE * (bit_first_zero(vmm->reserved[i]) +
                                 (i * BITMAP_BLOCK_SIZE));

            // NOTE: This line only works if we use 64 bytes long VMAs
            //       A page can contain up to 64 VMAs, so if index is odd
            //       the 32 previous (non-free) VMAs already belong to the same
            //       page (which means it is already allocated)
            if (i % 2 == 0)
                page_already_allocated = vmm->reserved[i] != 0x0;

            break;
        }
    }

    if (offset == (vaddr_t)-1) {
        log_err("VMM", "No space left in reserved memory");
        return 0;
    }

    vaddr_t address =
        (vmm == &kernel_vmm) ? KERNEL_VMM_RESERVED_START : VMM_RESERVED_START;

    address += offset;

    if (!page_already_allocated) {
        paddr_t pageframe = pmm_allocate(PMM_MAP_KERNEL);
        if (!mmu_map(address, pageframe, PROT_WRITE | PROT_READ)) {
            log_err("VMM",
                    "Virtual address for VMA already in use: " LOG_FMT_32,
                    address);
            pmm_free(pageframe);
            return 0;
        }
        memset((void *)address, 0, PAGE_SIZE);
    }

    bitmap_set(vmm->reserved, offset / VMA_SIZE);

    return address;
}

MAYBE_UNUSED static void vma_reserved_free(vmm_t *vmm, vma_t *vma)
{
    int index = (vmm == &kernel_vmm)
                  ? ((vaddr_t)vma - KERNEL_VMM_RESERVED_START) / VMA_SIZE
                  : ((vaddr_t)vma - VMM_RESERVED_START) / VMA_SIZE;

    bitmap_clear(vmm->reserved, index);

    // Free the page if no currently allocated VMA inside it
    // NOTE: This line only works if we use 64 bytes long VMAs (see allocate)

    const int offset = align_down(BITMAP_OFFSET(index), 2);

    if (vmm->reserved[offset] == 0x0 && vmm->reserved[offset + 1] == 0x0) {
        paddr_t pageframe = mmu_unmap(align_down((vaddr_t)vma, PAGE_SIZE));
        pmm_free(pageframe);
    }
}

bool vmm_init(vmm_t *vmm, vaddr_t start, vaddr_t end)
{
    // The VMM can only allocate address for pages
    if (start > end || end - start < PAGE_SIZE) {
        log_err("VMM", "init: VMM address space has invalid size (%ld)",
                end - start);
        return false;
    }

    if (start % PAGE_SIZE || end % PAGE_SIZE) {
        log_err("VMM",
                "init: start and end address must be page aligned (" LOG_FMT_32
                " -> " LOG_FMT_32 ")",
                start, end);
        return false;
    }

    // Cannot allocate virtual pages inside the reserved area(s)
    if (start < VMM_RESERVED_END ||
        RANGES_OVERLAP(start, end - 1, KERNEL_VMM_RESERVED_START,
                       KERNEL_VMM_RESERVED_END - 1)) {
        log_err("VMM",
                "init: invalid VMM range: [" LOG_FMT_32 ":" LOG_FMT_32 "]",
                start, end);
        return false;
    }

    vmm->start = start;
    vmm->end = end;

    memset(vmm->reserved, 0, sizeof(vmm->reserved));

    vma_t *first_area = (vma_t *)vma_reserved_allocate(vmm);
    first_area->start = start;
    first_area->size = (end - start);
    first_area->flags = 0x0;
    first_area->allocated = false;

    vmm->vmas.by_size = &first_area->avl.by_size;
    vmm->vmas.by_address = &first_area->avl.by_address;

    // TODO: Refactor such calls (hooks, initcalls, there are better ways to do)
    //       Even more so for this one since we'll be updating the interrupt
    //       handler each time we create a new process!
    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault));

    log_info("VMM",
             "Initialized VMM { start=" LOG_FMT_32 ", end=" LOG_FMT_32 " }",
             vmm->start, vmm->end);

    return true;
}

/**
 * AVL compare functions for the VMM
 *
 * The following functions are used by the VMM's AVL trees to determine which
 * path to take. They are all of the type @link avl_compare_t
 */

/* Determine if an area \c requested can be allocated from \c area */
static int vma_search_free_by_size(const avl_t *requested_avl,
                                   const avl_t *area_avl)
{
    vma_t *requested = container_of(requested_avl, vma_t, avl.by_size);
    vma_t *area = container_of(area_avl, vma_t, avl.by_size);

    // TODO: Best Fit algorithm
    if (!area->allocated && area->size >= requested->size)
        return 0;

    return 1;
}

/* Similar to @vma_search_free_by_size, but for the by_address tree */
static int vma_search_free_by_address(const avl_t *addr_avl,
                                      const avl_t *area_avl)
{
    const vma_t *addr = container_of(addr_avl, vma_t, avl.by_address);
    const vma_t *area = container_of(area_avl, vma_t, avl.by_address);

    if (IN_RANGE(addr->start, area->start, vma_end(area) - 1) &&
        !area->allocated) {
        return 0;
    }

    return (addr->start <= area->start) ? -1 : 1;
}

/**
 * Look for a free area of at least the given size and located after the
 * given address
 *
 * This is used when specifying a starting address to vmm_allocate
 */
static int vma_search_free_by_address_and_size(const avl_t *addr_avl,
                                               const avl_t *area_avl)
{
    const vma_t *addr = container_of(addr_avl, vma_t, avl.by_address);
    const vma_t *area = container_of(area_avl, vma_t, avl.by_address);

    if (area->start >= addr->start ||
        IN_RANGE(addr->start, area->start, vma_end(area) - 1)) {
        if (!area->allocated &&
            vma_end(area) >= MAX(area->start, addr->start) + addr->size)
            return 0;
        // We know all addresses higher than this one are valid,
        // we could do a best fit tho
        return 1;
    }

    return (addr->start <= area->start) ? -1 : 1;
}

/* Check if both areas are of the same size */
static int vma_compare_size(const avl_t *left_avl, const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_size);
    vma_t *right = container_of(right_avl, vma_t, avl.by_size);

    if (left->size == right->size) {
        // To be able to distinct in between areas of the same size
        RETURN_CMP(left->start, right->start);
    }

    return (left->size < right->size) ? -1 : 1;
}

/* Check if area @left is inside area @right */
static int vma_compare_address(const avl_t *left_avl, const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_address);
    vma_t *right = container_of(right_avl, vma_t, avl.by_address);

    if (IN_RANGE(left->start, right->start, right->start + right->size - 1))
        return 0;

    return (left->start < right->start) ? -1 : 1;
}

/* Similar to @vma_compare_address but for the by_size tree */
static int vma_compare_address_inside_size(const avl_t *left_avl,
                                           const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_size);
    vma_t *right = container_of(right_avl, vma_t, avl.by_size);

    if (IN_RANGE(left->start, right->start, right->start + right->size - 1))
        return 0;

    if (left->size == right->size) {
        // To be able to distinct in between areas of the same size
        RETURN_CMP(left->start, right->start);
    }

    return (left->size <= right->size) ? -1 : 1;
}

void vmm_split_vma(vmm_t *vmm, vma_t *original, vma_t *requested)
{
    if (requested->start > original->start) {
        /* We need to insert a new vma between the start of the original
         * area, and the start of the returned one.
         * This is the case when we found a suitable free area, right in the
         * middle of a bigger one, when specifying an explicit minimum virtual
         * address
         */
        vma_t *prepend = (vma_t *)vma_reserved_allocate(vmm);
        *prepend = (vma_t){
            .start = original->start,
            .size = requested->start - original->start,
            .allocated = false,
            .flags = original->flags,
        };

        avl_insert(&vmm->vmas.by_size, &prepend->avl.by_size, vma_compare_size);
        avl_insert(&vmm->vmas.by_address, &prepend->avl.by_address,
                   vma_compare_address);
    }

    vaddr_t new_start =
        MAX(original->start, requested->start) + requested->size;
    original->size -= (new_start - original->start);
    original->start = new_start;
    // cannot insert an old node in a tree, so reset it before doing so
    original->avl.by_address = AVL_EMPTY_NODE;
    original->avl.by_size = AVL_EMPTY_NODE;

    avl_insert(&vmm->vmas.by_size, &original->avl.by_size, vma_compare_size);
    avl_insert(&vmm->vmas.by_address, &original->avl.by_address,
               vma_compare_address);
}

vaddr_t vmm_allocate(vmm_t *vmm, vaddr_t addr, size_t size, int flags)
{
    if (size == 0)
        return 0x0;

    size = align_up(size, PAGE_SIZE);
    if (addr != 0)
        addr = align_up(addr, PAGE_SIZE);

    vma_t requested = {.size = size, .start = addr};
    const avl_t *area_avl;

    if (addr != 0) {
        area_avl = avl_remove(&vmm->vmas.by_address, &requested.avl.by_address,
                              vma_search_free_by_address_and_size);
    } else {
        area_avl = avl_remove(&vmm->vmas.by_size, &requested.avl.by_size,
                              vma_search_free_by_size);
    }

    if (area_avl == NULL) {
        log_err("VMM", "Failed to allocate");
        return VMM_INVALID;
    }

    vma_t *old;
    vma_t *allocated;

    if (addr != 0) {
        old = container_of(area_avl, vma_t, avl.by_address);
        avl_remove(&vmm->vmas.by_size, &old->avl.by_size, vma_compare_size);
    } else {
        old = container_of(area_avl, vma_t, avl.by_size);
        avl_remove(&vmm->vmas.by_address, &old->avl.by_address,
                   vma_compare_address);
    }

    if (old->size == size) {
        allocated = old;
    } else {
        allocated = (vma_t *)vma_reserved_allocate(vmm);
        *allocated = (vma_t){
            .start = MAX(addr, old->start),
            .size = size,
            .flags = flags,
        };

        // Reinsert into the trees the part of the original areas that were not
        // included inside the allocation
        vmm_split_vma(vmm, old, &requested);
    }

    // Insert the allocated virtual address inside the AVL tree
    // note: we do not keep track of the allocated areas inside by_size
    allocated->avl.by_address = AVL_EMPTY_NODE;
    avl_t *inserted = avl_insert(
        &vmm->vmas.by_address, &allocated->avl.by_address, vma_compare_address);

    if (IS_ERR(inserted)) {
        log_err("vmm", "failed to insert new VMA inside the AVL: %s",
                err_to_str(ERR_FROM_PTR(inserted)));
        return VMM_INVALID;
    }

    allocated->allocated = true;

    return allocated->start;
}

/** @brief Try to merge an area with another one present inside the VMM
 *
 * If an appropriate VMA to merge has been found, it will be removed from both
 * trees and it will be merged into @dst.
 *
 * @param vmm The VMM instance to use
 * @param dst The VMA to merge into
 * @param src_start The start address of the VMA we want to merge
 */
static void vma_try_merge(vmm_t *vmm, vma_t *dst, vaddr_t src_start)
{
    vma_t value = {.start = src_start};
    avl_t *avl = avl_remove(&vmm->vmas.by_address, &value.avl.by_address,
                            vma_search_free_by_address);
    if (avl != NULL) {
        vma_t *area = container_of(avl, vma_t, avl.by_address);
        // Remove the equivalent inside the by_size tree
        value.size = area->size;
        avl_remove(&vmm->vmas.by_size, &value.avl.by_size,
                   vma_compare_address_inside_size);
        // merge both areas into one
        dst->size += area->size;
        if (area->start < dst->start)
            dst->start = area->start;
    }
}

void vmm_free(vmm_t *vmm, vaddr_t addr, size_t length)
{

    addr = align_down(addr, PAGE_SIZE);
    length = align_up(length, PAGE_SIZE);

    // 1. Remove the corresponding area
    vma_t value = {.start = addr};
    avl_t *freed = avl_remove(&vmm->vmas.by_address, &value.avl.by_address,
                              vma_compare_address);

    vma_t *area = container_of(freed, vma_t, avl.by_address);
    area->allocated = false;

    // Merge with the previous area (if free)
    if (area->start == addr && area->start > vmm->start) {
        vma_try_merge(vmm, area, area->start - PAGE_SIZE);
    }

    // Merge with the next area (if free)
    if (addr + length == vma_end(area) && vma_end(area) < vmm->end) {
        vma_try_merge(vmm, area, area->start + area->size);
    }

    // Re-insert the merged free area inside the 2 AVL trees
    area->avl.by_address = AVL_EMPTY_NODE;
    area->avl.by_size = AVL_EMPTY_NODE;
    avl_insert(&vmm->vmas.by_address, &area->avl.by_address,
               vma_compare_address);
    avl_insert(&vmm->vmas.by_size, &area->avl.by_size, vma_compare_size);

    // It is possible that the requested length spans over multiple areas
    if (addr + length > vma_end(area) && vma_end(area) < vmm->end) {
        length -= vma_end(area) - addr;
        vmm_free(vmm, vma_end(area), length);
    }
}

const vma_t *vmm_find(vmm_t *vmm, vaddr_t addr)
{
    addr = align_down(addr, PAGE_SIZE);
    vma_t value = {.start = addr};

    const avl_t *vma = avl_search(vmm->vmas.by_address, &value.avl.by_address,
                                  vma_compare_address);

    if (vma == NULL)
        return NULL;

    return container_of(vma, vma_t, avl.by_address);
}

void vmm_destroy(vmm_t *vmm)
{
    if (vmm == &kernel_vmm) {
        log_err("VMM", "Trying to free the kernel VMM. Skipping.");
        return;
    }

    for (unsigned int i = 0; i < ARRAY_SIZE(vmm->reserved); i += 2) {
        // See vma_reserved_allocate for an explanation of what's going on
        if (*(u64 *)&vmm->reserved[i] != 0) {
            vaddr_t addr =
                VMM_RESERVED_START + (VMA_SIZE * i * BITMAP_BLOCK_SIZE);
            pmm_free(mmu_unmap(addr));
        }
    }

    kfree(vmm);
}

/// Structure of the page fault's error code
/// @link https://wiki.osdev.org/Exceptions#Page_Fault
typedef struct PACKED {
    u8 present : 1;
    u8 write : 1;
    u8 user : 1;
    u8 reserved_write : 1;
    u8 fetch : 1;
    u8 protection_key : 1;
    u8 ss : 1;
    u16 _unused1 : 8;
    u8 sgx : 1;
    u16 _unused2 : 15;
} page_fault_error;

/**
 * @brief Interrupt handler for page faults (#PF)
 * @ingroup vmm_internals
 *
 * What the handler does:
 * * Lazy allocation of pageframes for allocated virtual addresses
 * * Panic if address is effectively invalid
 *
 */
static DEFINE_INTERRUPT_HANDLER(page_fault)
{
    // The CR2 register holds the virtual address which caused the Page Fault
    vaddr_t faulty_address = read_cr2();

    vmm_t *vmm =
        IS_KERNEL_ADDRESS(faulty_address) ? &kernel_vmm : current_process->vmm;
    const vma_t *address_area = vmm_find(vmm, faulty_address);
    page_fault_error error = *(page_fault_error *)&frame.error;

    // Lazily allocate pageframes
    if (!error.present && address_area != NULL && address_area->allocated) {
        for (size_t off = 0; off < address_area->size; off += PAGE_SIZE) {
            const paddr_t pageframe = pmm_allocate(PMM_MAP_KERNEL);
            mmu_map(address_area->start + off, pageframe, address_area->flags);
            if (address_area->flags & MAP_CLEAR)
                memset((void *)address_area->start + off, 0, PAGE_SIZE);
        }
        return;
    }

    PANIC("PAGE FAULT at " LOG_FMT_32 ": %s access on a %s page %s",
          faulty_address, error.write ? "write" : "read",
          error.present ? "protected" : "non-present",
          error.user ? "while in user-mode" : "");
}

/**
 * @brief Implementation of the mmap syscall
 * @ingroup vmm_internals
 */
void *mmap(void *addr, size_t length, int prot, int flags)
{
    // The actual pageframes are lazily allocated by the #PF handler
    vmm_t *vmm = (flags & MAP_KERNEL) ? &kernel_vmm : current_process->vmm;
    return (void *)vmm_allocate(vmm, (vaddr_t)addr, length, prot | flags);
}

/**
 * @brief Implementation of the munmap syscall
 * @ingroup vmm_internals
 */
int munmap(void *addr, size_t length)
{
    if ((vaddr_t)addr % PAGE_SIZE)
        return -E_INVAL;

    // Mark virtual address as free
    vmm_t *vmm = IS_KERNEL_ADDRESS(addr) ? &kernel_vmm : current_process->vmm;
    vmm_free(vmm, (vaddr_t)addr, length);

    // Remove mappings from the page tables
    length = align_up(length, PAGE_SIZE);
    for (size_t off = 0; off < length; off += PAGE_SIZE) {
        paddr_t pageframe = mmu_unmap((vaddr_t)addr + off);
        if (pageframe != PMM_INVALID_PAGEFRAME)
            pmm_free(pageframe);
    }

    return E_SUCCESS;
}
