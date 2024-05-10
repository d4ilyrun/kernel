#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

#include <libalgo/avl.h>
#include <libalgo/bitmap.h>
#include <utils/bits.h>
#include <utils/container_of.h>
#include <utils/macro.h>
#include <utils/math.h>

/* For simplicity, we will allocate 64B for each VMA structure */
#define VMA_SIZE (64)
static_assert(sizeof(vma_t) <= VMA_SIZE,
              "Update the allocated size for VMA structures!");

static DEFINE_INTERRUPT_HANDLER(page_fault);

/**
 * @struct vmm
 * @ingroup VMM
 *
 * @brief Virtual Memory Manager
 *
 * A VMM is responsible for managing the allocated virtual addresses within a
 * process.
 *
 * The VMM splits the address space into distinct regions called Virtual Memory
 * Areas, and keeps track of them individually using AVL trees.
 *
 * A VMM does not necessarily manage the full 32bits address space, instead it
 * is assigned a start and end addresses, and only keeps track of the regions
 * inside this range.
 *
 * The VMM uses two different AVL trees to keep track of the VMAs:
 *  *   Ordered by address to easily retrieve the area to which an address
 *      belongs.
 *  *   Ordered by size to retrieve the first fitting area when allocating
 */
typedef struct vmm {

    vaddr_t start; /*!< The start of the VMM's assigned range */
    vaddr_t end;   /*!< The end of the VMM's assigned range (excluded) */

    /** Roots of the AVL trees containing the VMAs */
    struct vmm_vma_roots {
        avl_t *by_address;
        avl_t *by_size;
    } vmas;

    /** Bitmap of the available virtual addreses inside the reserved area
     *
     *  TODO: Using a bitmap for this takes 2KiB of memory per VMM (so per
     * process)! Is there a less expensive way to keep track of them?
     */
    BITMAP(reserved, VMM_RESERVED_SIZE / VMA_SIZE);

} vmm_t;

/**
 * @defgroup vmm_internals VMM Internals
 * @ingroup VMM
 *
 * Internal functions and variables used by the VMM
 *
 * These are just used by ur implementation and should not be accessed by any
 * other part of the kernel
 */

// TODO: Don't use a static VMM ! (multiprocess)
static vmm_t kernel_vmm;

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
    vaddr_t addr = 0;
    bool page_already_allocated = true;

    // Find the first available virtual address within the reserved range
    for (unsigned int i = 0; i < ARRAY_SIZE(vmm->reserved); ++i) {
        if (vmm->reserved[i] != (bitmap_block_t)-1) {
            addr = VMM_RESERVED_START +
                   VMA_SIZE * (bit_first_zero(vmm->reserved[i]) +
                               (i * BITMAP_BLOCK_SIZE));

            // NOTE: This line only works if we use 64 bytes long VMAs
            //       A page is 64 VMAs, so if index is odd, the 32 previous VMAs
            //       already belong to the same page (which means it is
            //       allocated)
            if (i % 2 == 0)
                page_already_allocated = vmm->reserved[i] != 0x0;

            break;
        }
    }

    if (!addr) {
        log_err("VMM", "No space left in reserved memory");
        return 0;
    }

    if (!page_already_allocated) {
        paddr_t pageframe = pmm_allocate(PMM_MAP_KERNEL);
        if (!mmu_map(addr, pageframe)) {
            log_err("VMM",
                    "Virtual address for VMA already in use: " LOG_FMT_32,
                    addr);
            pmm_free(pageframe);
            return 0;
        }
    }

    bitmap_set(vmm->reserved, (addr - VMM_RESERVED_START) / VMA_SIZE);

    return addr;
}

MAYBE_UNUSED static void vma_reserved_free(vmm_t *vmm, vma_t *vma)
{
    const int index = ((vaddr_t)vma - VMM_RESERVED_START) / VMA_SIZE;
    const int offset = BITMAP_OFFSET(index) % 2;

    bitmap_clear(vmm->reserved, index);

    // Free the page if no currently allocated VMA inside it
    // NOTE: This line only works if we use 64 bytes long VMAs (see allocate)
    if (vmm->reserved[offset] == 0x0 && vmm->reserved[offset + 1] == 0x0) {
        paddr_t pageframe = mmu_unmap(align_down((vaddr_t)vmm, PAGE_SIZE));
        pmm_free(pageframe);
    }
}

bool vmm_init(vaddr_t start, vaddr_t end)
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

    // Cannot allocate virtual pages inside the reserved area
    if (start < VMM_RESERVED_END) {
        log_err("VMM", "init: invalid VMM range");
        return false;
    }

    kernel_vmm.start = start;
    kernel_vmm.end = end;

    vma_t *first_area = (vma_t *)vma_reserved_allocate(&kernel_vmm);
    first_area->start = start;
    first_area->size = (end - start);
    first_area->flags = 0x0;
    first_area->allocated = false;

    kernel_vmm.vmas.by_size = &first_area->avl.by_size;
    kernel_vmm.vmas.by_address = &first_area->avl.by_address;

    // TODO: Refactor such calls (hooks, initcalls, there are better ways to do)
    //       Even more so for this one since we'll be updating the interrupt
    //       handler each time we create a new process!
    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault));

    log_info("VMM",
             "Initialized VMM { start=" LOG_FMT_32 ", end=" LOG_FMT_32 "}",
             kernel_vmm.start, kernel_vmm.end);

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

vaddr_t vmm_allocate(size_t size, int flags)
{
    size = align_up(size, PAGE_SIZE);

    vma_t requested = {.size = size};
    const avl_t *area_avl =
        avl_remove(&kernel_vmm.vmas.by_size, &requested.avl.by_size,
                   vma_search_free_by_size);

    if (area_avl == NULL) {
        log_err("VMM", "Failed to allocate");
        return VMM_INVALID;
    }

    vma_t *old = container_of(area_avl, vma_t, avl.by_size);
    vma_t *allocated;

    avl_remove(&kernel_vmm.vmas.by_address, &old->avl.by_address,
               vma_compare_address);

    if (old->size == size) {
        allocated = old;
    } else {

        allocated = (vma_t *)vma_reserved_allocate(&kernel_vmm);
        *allocated = (vma_t){
            .start = old->start,
            .size = size,
            .allocated = true,
            .flags = flags,
        };

        // There still is space remaining inside the original area,
        // so we can reinsert it into the trees
        old->start += size;
        old->size -= size;

        // cannot insert an old node in a tree, so reset it before doing so
        old->avl.by_size = AVL_EMPTY_NODE;
        old->avl.by_address = AVL_EMPTY_NODE;

        avl_insert(&kernel_vmm.vmas.by_size, &old->avl.by_size,
                   vma_compare_size);
        avl_insert(&kernel_vmm.vmas.by_address, &old->avl.by_address,
                   vma_compare_address);
    }

    // Insert the allocated virtual address inside the AVL tree
    avl_insert(&kernel_vmm.vmas.by_size, &allocated->avl.by_size,
               vma_compare_size);
    avl_insert(&kernel_vmm.vmas.by_address, &allocated->avl.by_address,
               vma_compare_address);

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

void vmm_free(vaddr_t addr)
{

    addr = align_down(addr, PAGE_SIZE);

    // 1. Remove the corresponding area
    vma_t value = {.start = addr};
    avl_t *freed = avl_remove(&kernel_vmm.vmas.by_address,
                              &value.avl.by_address, vma_compare_address);
    vma_t *area = container_of(freed, vma_t, avl.by_address);

    // Remove the equivalent inside the by_size tree
    value.size = area->size;
    avl_remove(&kernel_vmm.vmas.by_size, &value.avl.by_size,
               vma_compare_address_inside_size);

    // 2. Merge with surrounding areas if possible

    area->allocated = false;

    // Avoid negative overflow of uselessly going through the AVL
    if (area->start > (kernel_vmm.start)) {
        // any value in [-PAGE_SIZE; -1] would work
        vma_try_merge(&kernel_vmm, area, area->start - PAGE_SIZE);
    }

    // Same, avoid overflow
    if (area->start < kernel_vmm.end - PAGE_SIZE) {
        vma_try_merge(&kernel_vmm, area, area->start + area->size);
    }

    // Re-insert the merged free area inside the 2 AVL trees
    area->avl.by_address = AVL_EMPTY_NODE;
    area->avl.by_size = AVL_EMPTY_NODE;
    avl_insert(&kernel_vmm.vmas.by_address, &area->avl.by_address,
               vma_compare_address);
    avl_insert(&kernel_vmm.vmas.by_size, &area->avl.by_size, vma_compare_size);
}

const vma_t *vmm_find(vaddr_t addr)
{
    addr = align_down(addr, PAGE_SIZE);
    vma_t value = {.start = addr};

    const avl_t *vma = avl_search(kernel_vmm.vmas.by_address,
                                  &value.avl.by_address, vma_compare_address);

    if (vma == NULL)
        return NULL;

    return container_of(vma, vma_t, avl.by_address);
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
 * @todo lazy allocation (allocate pageframe if PF and allocated by VMM)
 */
static DEFINE_INTERRUPT_HANDLER(page_fault)
{
    log_warn("interrupt", "Interrupt recieved: Page fault");
    page_fault_error error = *(page_fault_error *)&frame.error;

    log_dbg("[PF] source", "%s access on a %s page %s",
            error.write ? "write" : "read",
            error.present ? "protected" : "non-present",
            error.user ? "while in user-mode" : "");

    // The CR2 register holds the virtual address which caused the Page Fault
    vaddr_t faulty_address = read_cr2();

    log_dbg("[PF] error", LOG_FMT_32, frame.error);
    log_dbg("[PF] address", LOG_FMT_32, faulty_address);

    PANIC("PAGE FAULT at " LOG_FMT_32 ": %s access on a %s page %s",
          faulty_address, error.write ? "write" : "read",
          error.present ? "protected" : "non-present",
          error.user ? "while in user-mode" : "");
}
