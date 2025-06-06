/**
 * @brief Virtual Memory Manager
 *
 * @file kerne/memory/vmm.h
 * @author Léo DUBOIN <leo@duboin.com>
 * @date 28/04/2024
 *
 * @defgroup VMM Virtual Memory Manager
 * @ingroup kernel
 *
 * # VMM
 *
 * ## Description
 *
 * The virtual memory manager is responsible for handling the available virtual
 * addresses.
 *
 * It has to keep track of which virtual addresses have already been allocated,
 * as well as per-region flags and parameters (file-mapped, kernel, mmio, ...).
 * This is done per-process, each one having their own VMM.
 *
 * This should be dinstinguished from the PMM and MMU. All three work
 * **together**, but serve their own respective purpose. Please refer to the
 * corresponding header and code for information about those.
 *
 * ## Implementation
 *
 * The virtual memory allocator keeps track not of singular addresses, but of
 * Virtual Memory Areas (VMAs).
 *
 * We store these areas inside an AVL tree, as it allows for faster lookup
 * operations. We use two different trees to store these, one ordered by the
 * size of the area, the other ordered by its address. The areas inside both
 * trees are the same, we chose between them depending on the type of operation
 * we need to perform.
 *
 * For allocating the structs which constitute these trees, we reserve a 32kiB
 * area within the current virtual space which will be used only to address
 * our underlying structs.
 *
 * @note This is only the first design. The way we handle memory regions will
 * surely change in the future as I learn more and more features are implemented
 * in the kernel. Here's a exhaustive list of things that are subject to change:
 * * Should we keep using 2 trees: twice as long to modify, but lookup is faster
 * * One region per allocation? Can't it grow too big?
 * * Keep a track of the pageframes inside the regions will become necessary
 * * Getting rid of the bitmap inside the VMM struct (how?)
 *
 * @{
 */

#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <kernel/memory.h>
#include <kernel/spinlock.h>
#include <kernel/types.h>
#include <kernel/vm.h>

#include <libalgo/avl.h>
#include <libalgo/bitmap.h>
#include <libalgo/linked_list.h>
#include <utils/compiler.h>
#include <utils/container_of.h>

#include <stdbool.h>
#include <stddef.h>

struct file;

/**
 * @brief Virtual Memory Area
 *
 * @struct vma
 *
 * A virtual memory area represents a contiguous area in the virtual address
 * space. A single area can be allocated (currently in use), or not. If an area
 * is marked as 'free', this means that each (virtual) addresses located inside
 * it can safely be given to a function which requests it.
 *
 * Each area can also be associated with different feature flags (defined as
 * VMM_F_*). This is necessary to be able distinct between addresses mapped to
 * files or not, or even addresses mapped to IO devices.
 *
 * VMAs are managed by the Virtual Memory Manager, and should not be interacted
 * with directly by the user. They serve as an internal structure for the VMM to
 * be able to easily keep track of allocated addresses.
 *
 * @link avl_t Internal data, used to go through the VMM's AVL tree.
 */
typedef struct vma {

    struct vm_segment segment; /*!< Used by the address space API */

    bool allocated; /*!< Whether this area is currently being used */

    /**
     * @brief Intrusive AVL tree structures used by the VMM
     * @link libalgo/avl.H
     */
    struct vma_avl {
        struct avl by_address; /*!< AVL tree ordered by address */
        struct avl by_size;    /*!< AVL tree ordered by size */
    } avl;

} vma_t;

/* For simplicity, we will allocate 64B for each VMA structure */
#define VMA_SIZE (64)
static_assert(sizeof(vma_t) <= VMA_SIZE, "Update the allocated size for VMA "
                                         "structures!");

/** Compute the end address of a VMA. */
static inline vaddr_t vma_end(const vma_t *vma)
{
    return segment_end(&vma->segment);
}

/** @return the start address of a VMA. */
static inline vaddr_t vma_start(const vma_t *vma)
{
    return vma->segment.start;
}

static inline size_t vma_size(const vma_t *vma)
{
    return vma->segment.size;
}

static inline u32 vma_flags(const vma_t *vma)
{
    return vma->segment.flags;
}

/**
 * @struct vmm
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
 *
 * @note The tree ordered by size does not keep track of used areas, since we
 * never need to search for un-free area by size. This avoid modifying the
 * trees more than we need to, since it can be costly.
 */
typedef struct vmm {

    vaddr_t start; /*!< The start of the VMM's assigned range */
    vaddr_t end;   /*!< The end of the VMM's assigned range (excluded) */

    struct address_space *as;

    /** Roots of the AVL trees containing the VMAs */
    struct vmm_vma_roots {
        avl_t *by_address;
        avl_t *by_size;
    } vmas;

    spinlock_t lock; /*!< Used restrict access to the VMM when modifying it */

    /** Bitmap of the available virtual addreses inside the reserved area
     *
     *  TODO: Using a bitmap for this takes 2KiB of memory per VMM (so per
     * process)! Is there a less expensive way to keep track of them?
     */
    BITMAP(reserved, VMM_RESERVED_SIZE / VMA_SIZE);

} vmm_t;

/** Returned by VMM functions in case of error */
#define MMAP_INVALID NULL

/**
 * Global kernel VMM, used to allocate shared kernel addresses.
 *
 * These addresses are stored in the PTEs above KERNEL_VIRTUAL_START, and are
 * shared across all processes. That is why we must use a global shared VMM.
 */
extern vmm_t kernel_vmm;

/** Check whether a virtual address has been allocated using @ref kernel_vmm
 *  @ingroup vmm_internals
 */
#define IS_KERNEL_ADDRESS(_addr) \
    IN_RANGE((vaddr_t)(_addr), KERNEL_MEMORY_START, KERNEL_MEMORY_END)

/* Allocate a new VMM structure */
struct vmm *vmm_new(struct address_space *);

/**
 * @brief Initialize a VMM instance
 *
 * @param vmm The VMM instance
 * @param start The starting address of the VMM's range
 * @param end The end address of the VMM's range (excluded)
 *
 * @return Whether the init processes suceeded or not
 */
bool vmm_init(vmm_t *vmm, vaddr_t start, vaddr_t end);

/** @brief Copy the content of a VMM instance inside another one.
 *
 * This function only copies the VMM's metadata. The actual content
 * of the address space managed by the VMM should be duplicated using
 * the CoW mechanism (@see mmu_clone).
 */
void vmm_copy(vmm_t *dst, vmm_t *src);

/**
 * @brief Allocate a virtual area of the given size
 *
 * The minimum addressable size for the VMM is a page. The size of the allocated
 * area will automatically be rounded to the next page size.
 *
 * You can specify a minimum virtual address to use for the area with the @c
 * addr parameter. If not NULL the returned address is **guaranted** to be
 * located at or after the specified one, else the kernel will chose one.
 *
 * @param vmm The VMM instance to use
 * @param addr Starting address for the allocated area.
 * @param size The size of the requested area
 * @param flags Feature flags used for the allocated area.
 *              Must be a combination of @link vma_flags @endlink
 *
 * @return The virtual start address of the area, or NULL
 */
struct vm_segment *vmm_allocate(vmm_t *, vaddr_t, size_t size, int flags);

/**
 * @brief Free a virtual address
 *
 * @warning This does not free the underlying page nor the PTE entry.
 *          All it does is mark the corresponding VMA as available for later
 *          allocations.
 */
void vmm_free(vmm_t *, vaddr_t, size_t length);

/**
 * @brief Find the VMA to which a virtual address belongs
 * @return The VMA containing this address, or NULL if not found
 */
struct vm_segment *vmm_find(const vmm_t *, vaddr_t);

/** Release all the VMAs inside a VMM instance.
 *
 *  @warning This does not release the actual virtual addresses referenced
 *  by the VMAs, please make sure to release it at some point.
 */
void vmm_clear(vmm_t *vmm);

/** Free the VMM struct
 *  @note You should release its content using @ref vmm_clear before calling
 *        this function
 */
void vmm_destroy(vmm_t *vmm);

/**
 * Map a file into kernel memory
 *
 * @param file The file to be mapped
 * @param prot Protection flags for the mapping.
 *             Must be a combination of @ref mmu_prot
 */
void *map_file(struct file *file, int prot);

/**
 * Delete a file's memory mapping.
 *
 * @param file The memory mapped file
 * @param addr The starting address of the mapped memory
 *
 * The starting address MUST be page aligned (EINVAL).
 *
 * @return A value of type @ref error_t
 *
 * @info Every page that the range is inside of will be unmaped, even if it's
 *       only for one byte. Beware of overflows and mind the alignment!
 */
error_t unmap_file(struct file *file, void *addr);

#endif /* KERNEL_VMM_H */

/** @} */
