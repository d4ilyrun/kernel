#define LOG_DOMAIN "vmm"

#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
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

#include <string.h>

/* For simplicity, we will allocate 64B for each VMA structure */
#define VMA_SIZE (64)
static_assert(sizeof(vma_t) <= VMA_SIZE, "Update the allocated size for VMA "
                                         "structures!");

static DEFINE_INTERRUPT_HANDLER(page_fault);

static inline struct vma *to_vma_by_address(const struct avl *avl)
{
    return container_of(avl, struct vma, avl.by_address);
}

static inline struct vma *to_vma_by_size(const struct avl *avl)
{
    return container_of(avl, struct vma, avl.by_size);
}

/**
 * @defgroup vmm_internals VMM Internals
 * @ingroup VMM
 *
 * Internal functions and variables used by the VMM
 *
 * These are just used by ur implementation and should not be accessed by any
 * other part of the kernel
 */

vmm_t kernel_vmm = {
    .as = &kernel_address_space,
};

static void vmm_lock(struct vmm *vmm)
{
    spinlock_acquire(&vmm->lock);
}

static void vmm_unlock(struct vmm *vmm)
{
    spinlock_release(&vmm->lock);
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
        log_err("No space left in reserved memory");
        return 0;
    }

    vaddr_t address = (vmm == &kernel_vmm) ? KERNEL_VMM_RESERVED_START
                                           : VMM_RESERVED_START;

    address += offset;

    if (!page_already_allocated) {
        paddr_t pageframe = pmm_allocate();
        if (!mmu_map(address, pageframe,
                     PROT_WRITE | PROT_READ | PROT_KERNEL)) {
            log_err("Virtual address for VMA already in use: " FMT32, address);
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

struct vmm *vmm_new(struct address_space *as)
{
    struct vmm *new = kcalloc(1, sizeof(struct vmm), KMALLOC_KERNEL);
    if (new == NULL)
        return PTR_ERR(E_NOMEM);
    new->as = as;
    return new;
}

bool vmm_init(vmm_t *vmm, vaddr_t start, vaddr_t end)
{
    // The VMM can only allocate address for pages
    if (start > end || end - start < PAGE_SIZE) {
        log_err("init: VMM address space has invalid size (%d)", end - start);
        return false;
    }

    if (start % PAGE_SIZE || end % PAGE_SIZE) {
        log_err("init: start and end address must be page aligned (" FMT32
                " -> " FMT32 ")",
                start, end);
        return false;
    }

    // Cannot allocate virtual pages inside the reserved area(s)
    if (start < VMM_RESERVED_END ||
        RANGES_OVERLAP(start, end - 1, KERNEL_VMM_RESERVED_START,
                       KERNEL_VMM_RESERVED_END - 1)) {
        log_err("init: invalid VMM range: [" FMT32 ":" FMT32 "]", start, end);
        return false;
    }

    vmm->start = start;
    vmm->end = end;

    memset(vmm->reserved, 0, sizeof(vmm->reserved));

    vma_t *first_area = (vma_t *)vma_reserved_allocate(vmm);
    first_area->segment.start = start;
    first_area->segment.size = (end - start);
    first_area->segment.flags = 0x0;
    first_area->allocated = false;

    vmm->vmas.by_size = &first_area->avl.by_size;
    vmm->vmas.by_address = &first_area->avl.by_address;

    INIT_SPINLOCK(vmm->lock);

    // TODO: Refactor such calls (hooks, initcalls, there are better ways to do)
    //       Even more so for this one since we'll be updating the interrupt
    //       handler each time we create a new process!
    interrupts_set_handler(PAGE_FAULT, INTERRUPT_HANDLER(page_fault), NULL);

    log_info("Initialized VMM { start=" FMT32 ", end=" FMT32 " }", vmm->start,
             vmm->end);

    return true;
}

void vmm_copy(vmm_t *dst, vmm_t *src)
{
    vmm_lock(src);
    vmm_lock(dst);

    dst->start = src->start;
    dst->end = dst->end;

    /*
     * Replace the destination VMM's VMAs, and release the old ones.
     * Their actual content should be copied through CoW.
     */
    vmm_clear(dst);
    dst->vmas.by_address = src->vmas.by_address;
    dst->vmas.by_size = src->vmas.by_size;

    memcpy(dst->reserved, src->reserved, sizeof(src->reserved));

    vmm_unlock(dst);
    vmm_unlock(src);
}

static void vmm_print_node_by_size(const struct avl *node)
{
    struct vma *vma = to_vma_by_size(node);
    printk("[%x - %x] %s\n", vma_start(vma), vma_end(vma),
           vma->allocated ? "allocated" : "free");
}

static void vmm_print_node_by_address(const struct avl *node)
{
    struct vma *vma = to_vma_by_address(node);
    printk("%ld %s bytes @ %x\n", vma_size(vma),
           vma->allocated ? "used" : "free", vma_start(vma));
}

MAYBE_UNUSED static void vmm_dump(vmm_t *vmm)
{
    log_dbg("%p@by_size", vmm);
    avl_print(vmm->vmas.by_size, vmm_print_node_by_size);
    log_dbg("%p@by_size", vmm);
    avl_print(vmm->vmas.by_address, vmm_print_node_by_address);
}

/**
 * AVL compare functions for the VMM
 *
 * The following functions are used by the VMM's AVL trees to determine which
 * path to take. They are all of the type @link avl_compare_t
 */

/* Determine if an area \c requested can be allocated from \c area */
static int
vma_search_free_by_size(const avl_t *requested_avl, const avl_t *area_avl)
{
    vma_t *requested = container_of(requested_avl, vma_t, avl.by_size);
    vma_t *area = container_of(area_avl, vma_t, avl.by_size);

    // TODO: Best Fit algorithm
    if (!area->allocated && vma_size(area) >= vma_size(requested))
        return 0;

    return 1;
}

/* Similar to @vma_search_free_by_size, but for the by_address tree */
static int
vma_search_free_by_address(const avl_t *addr_avl, const avl_t *area_avl)
{
    const vma_t *addr = container_of(addr_avl, vma_t, avl.by_address);
    const vma_t *area = container_of(area_avl, vma_t, avl.by_address);

    if (IN_RANGE(vma_start(addr), vma_start(area), vma_end(area) - 1) &&
        !area->allocated) {
        return 0;
    }

    return (vma_start(addr) <= vma_start(area)) ? -1 : 1;
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

    if (vma_start(area) >= vma_start(addr) ||
        IN_RANGE(vma_start(addr), vma_start(area), vma_end(area) - 1)) {
        if (!area->allocated &&
            vma_end(area) >=
                MAX(vma_start(area), vma_start(addr)) + vma_size(addr))
            return 0;
        // We know all addresses higher than this one are valid,
        // we could do a best fit tho
        return 1;
    }

    return (vma_start(addr) <= vma_start(area)) ? -1 : 1;
}

/* Check if both areas are of the same size */
static int vma_compare_size(const avl_t *left_avl, const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_size);
    vma_t *right = container_of(right_avl, vma_t, avl.by_size);

    if (vma_size(left) == vma_size(right)) {
        // To be able to distinct in between areas of the same size
        RETURN_CMP(vma_start(left), vma_start(right));
    }

    return (vma_size(left) < vma_size(right)) ? -1 : 1;
}

/* Check if area @left is inside area @right */
static int vma_compare_address(const avl_t *left_avl, const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_address);
    vma_t *right = container_of(right_avl, vma_t, avl.by_address);

    if (IN_RANGE(vma_start(left), vma_start(right), vma_end(right) - 1))
        return 0;

    return (vma_start(left) < vma_start(right)) ? -1 : 1;
}

/* Similar to @vma_compare_address but for the by_size tree */
static int
vma_compare_address_inside_size(const avl_t *left_avl, const avl_t *right_avl)
{
    vma_t *left = container_of(left_avl, vma_t, avl.by_size);
    vma_t *right = container_of(right_avl, vma_t, avl.by_size);

    if (IN_RANGE(vma_start(left), vma_start(right), vma_end(right) - 1))
        return 0;

    if (vma_size(left) == vma_size(right)) {
        // To be able to distinct in between areas of the same size
        RETURN_CMP(vma_start(left), vma_start(right));
    }

    return (vma_size(left) <= vma_size(right)) ? -1 : 1;
}

/* Extract a sub-area from a larger one.
 *
 * The area(s) that are not part of the extracted one are auomatically
 * re-inserted inside the containing trees. Make sure that the 'original'
 * area has been removed from the trees when calling this function.
 *
 * note: we do not keep track of the allocated areas inside by_size
 */
static void vmm_extract_vma(vmm_t *vmm, vma_t *original, vma_t *requested)
{

    // 1. If the extracted area starts after the beginning, prepend sub-area
    if (vma_start(requested) > vma_start(original)) {
        vma_t *prepend = (vma_t *)vma_reserved_allocate(vmm);
        *prepend = (vma_t){
            .allocated = original->allocated,
            .segment = {
                .start = vma_start(original),
                .size = vma_start(requested) - vma_start(original),
                .flags = vma_flags(original),
            }};

        avl_insert(&vmm->vmas.by_address, &prepend->avl.by_address,
                   vma_compare_address);
        if (!prepend->allocated)
            avl_insert(&vmm->vmas.by_size, &prepend->avl.by_size,
                       vma_compare_size);
    }

    // 2. If the extracted area ends before the end, append a smaller area
    if (vma_end(requested) == vma_end(original)) {
        vma_reserved_free(vmm, original);
        return;
    }

    original->segment.size = vma_end(original) - vma_end(requested);
    original->segment.start = vma_end(requested);

    // cannot insert an old node in a tree, so reset it before doing so
    original->avl.by_address = AVL_EMPTY_NODE;
    original->avl.by_size = AVL_EMPTY_NODE;

    avl_insert(&vmm->vmas.by_address, &original->avl.by_address,
               vma_compare_address);
    if (!original->allocated)
        avl_insert(&vmm->vmas.by_size, &original->avl.by_size,
                   vma_compare_size);
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
    vma_t value = {.segment = {.start = src_start}};
    avl_t *avl = avl_remove(&vmm->vmas.by_address, &value.avl.by_address,
                            vma_search_free_by_address);
    if (avl != NULL) {
        vma_t *area = container_of(avl, vma_t, avl.by_address);
        // Remove the equivalent inside the by_size tree
        value.segment.size = vma_size(area);
        avl_remove(&vmm->vmas.by_size, &value.avl.by_size,
                   vma_compare_address_inside_size);
        // merge both areas into one
        dst->segment.size += vma_size(area);
        if (vma_start(area) < vma_start(dst))
            dst->segment.start = vma_start(area);
    }
}

struct vm_segment *
vmm_allocate(vmm_t *vmm, vaddr_t addr, size_t size, int flags)
{
    vma_t requested;
    const avl_t *area_avl;
    vma_t *allocated;

    if (size == 0)
        return PTR_ERR(E_INVAL);

    size = align_up(size, PAGE_SIZE);
    if (addr != 0)
        addr = align_up(addr, PAGE_SIZE);

    requested = (vma_t){.segment = {.size = size, .start = addr}};

    vmm_lock(vmm);

    // Look for a large enough free area. If specified a starting address,
    // the area's starting address must be superior or equal to it.
    if (addr != 0) {
        area_avl = avl_remove(&vmm->vmas.by_address, &requested.avl.by_address,
                              vma_search_free_by_address_and_size);
    } else {
        area_avl = avl_remove(&vmm->vmas.by_size, &requested.avl.by_size,
                              vma_search_free_by_size);
    }

    if (area_avl == NULL) {
        log_err("failed to find a suitable free area");
        vmm_unlock(vmm);
        return PTR_ERR(E_INVAL);
    }

    // We also need to remove the newly found area from the other tree than
    // the one used to find it
    if (addr != 0) {
        allocated = container_of(area_avl, vma_t, avl.by_address);
        avl_remove(&vmm->vmas.by_size, &allocated->avl.by_size,
                   vma_compare_size);
    } else {
        allocated = container_of(area_avl, vma_t, avl.by_size);
        avl_remove(&vmm->vmas.by_address, &allocated->avl.by_address,
                   vma_compare_address);
    }

    // In case the suitable area is located inside a larger area, we need to
    // extract it from there, and split the original area into multiple sub-ones
    //
    // This can be the case when:
    // - Specified an explicit address
    // - The area is larger than the required size
    if (vma_size(allocated) != size) {
        vma_t *original = allocated;
        allocated = (vma_t *)vma_reserved_allocate(vmm);
        *allocated = (vma_t){
            .segment =
                {
                    .start = MAX(addr, vma_start(original)),
                    .size = size,
                    .flags = flags,
                },
        };

        // Reinsert into the trees the part of the original areas that were not
        // included inside the allocation
        vmm_extract_vma(vmm, original, allocated);
    }

    // Insert the allocated virtual address inside the AVL tree
    // note: we do not keep track of the allocated areas inside by_size
    allocated->avl.by_address = AVL_EMPTY_NODE;
    avl_t *inserted = avl_insert(
        &vmm->vmas.by_address, &allocated->avl.by_address, vma_compare_address);

    if (IS_ERR(inserted)) {
        log_err("failed to insert new VMA inside the AVL: %s",
                err_to_str(ERR_FROM_PTR(inserted)));
        vmm_unlock(vmm);
        return (void *)inserted;
    }

    vm_segment_insert(vmm->as, &allocated->segment);

    allocated->allocated = true;

    vmm_unlock(vmm);

    return &allocated->segment;
}

void vmm_free(vmm_t *vmm, vaddr_t addr, size_t length)
{

    addr = align_down(addr, PAGE_SIZE);
    length = align_up(length, PAGE_SIZE);

    // 1. Remove the corresponding area
    vma_t requested = {.segment = {.start = addr, .size = length}};
    avl_t *freed;

    vmm_lock(vmm);

    freed = avl_remove(&vmm->vmas.by_address, &requested.avl.by_address,
                       vma_compare_address);
    if (freed == NULL)
        goto vmm_release_lock;

    vma_t *area = container_of(freed, vma_t, avl.by_address);
    vm_segment_remove(vmm->as, &area->segment);

    // If only freeing part of the area, extract the part of interest
    if (vma_size(area) != length) {
        vma_t *original = area;
        area = (vma_t *)vma_reserved_allocate(vmm);
        *area = requested;
        area->segment.flags = vma_flags(original);
        vmm_extract_vma(vmm, original, area);
    }

    area->allocated = false;

    // Merge with the next area (if free)
    if (vma_end(area) == vma_end(&requested) && vma_end(area) < vmm->end) {
        vma_try_merge(vmm, area, vma_end(area));
    }

    // Merge with the previous area (if free)
    if (vma_start(area) == vma_start(&requested) &&
        vma_start(area) > vmm->start) {
        vma_try_merge(vmm, area, vma_start(area) - PAGE_SIZE);
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

vmm_release_lock:
    vmm_unlock(vmm);
}

struct vm_segment *vmm_find(const vmm_t *vmm, vaddr_t addr)
{
    vmm_lock((vmm_t *)vmm);

    addr = align_down(addr, PAGE_SIZE);
    vma_t value = {.segment = {.start = addr}};

    const avl_t *vma = avl_search(vmm->vmas.by_address, &value.avl.by_address,
                                  vma_compare_address);

    vmm_unlock((vmm_t *)vmm);

    if (vma == NULL)
        return NULL;

    return &container_of(vma, vma_t, avl.by_address)->segment;
}

void vmm_clear(vmm_t *vmm)
{
    if (vmm == &kernel_vmm) {
        log_err("Trying to free the kernel VMM. Skipping.");
        return;
    }

    vmm_lock((vmm_t *)vmm);

    // Freeing all the pages allocated for storing the VMAs
    // See vma_reserved_allocate for an explanation of what's going on
    for (unsigned int i = 0; i < ARRAY_SIZE(vmm->reserved); i += 2) {
        if (*(u64 *)&vmm->reserved[i] != 0) {
            vaddr_t addr = VMM_RESERVED_START +
                           (VMA_SIZE * i * BITMAP_BLOCK_SIZE);
            paddr_t page = mmu_unmap(addr);
            if (page != PMM_INVALID_PAGEFRAME)
                pmm_free(page);
        }
    }

    vmm_unlock(vmm);
}

void vmm_destroy(vmm_t *vmm)
{
    if (vmm == &kernel_vmm) {
        log_err("Trying to destroy the kernel VMM. Skipping.");
        return;
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

static DEFINE_INTERRUPT_HANDLER(page_fault)
{
    // The CR2 register holds the virtual address which caused the Page Fault
    void *faulty_address = (void *)read_cr2();
    interrupt_frame *frame = data;
    page_fault_error error = *(page_fault_error *)&frame->error;
    bool is_cow = error.present && error.write;
    struct address_space *as;

    if (!error.present || is_cow) {
        as = IS_KERNEL_ADDRESS(faulty_address) ? &kernel_address_space
                                               : current->process->as;
        return address_space_fault(as, faulty_address, is_cow);
    }

    PANIC("PAGE FAULT at " FMT32 ": %s access on a %s page %s", faulty_address,
          error.write ? "write" : "read",
          error.present ? "protected" : "non-present",
          error.user ? "while in user-mode" : "");
}

void *map_file(struct file *file, int prot)
{
    size_t length;
    void *memory;
    error_t err;

    length = align_up(file_size(file), PAGE_SIZE);
    memory = vm_alloc(&kernel_address_space, length, prot);
    if (IS_ERR(memory))
        return MMAP_INVALID;

    err = file_read(file, memory, file_size(file));
    if (err) {
        vm_free(&kernel_address_space, memory);
        return MMAP_INVALID;
    }

    return memory;
}

error_t unmap_file(struct file *file, void *addr)
{
    UNUSED(file);

    if ((vaddr_t)addr % PAGE_SIZE)
        return E_INVAL;

    if (addr == MMAP_INVALID)
        return E_SUCCESS;

    vm_free(&kernel_address_space, addr);

    return E_SUCCESS;
}
