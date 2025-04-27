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

#include <stdbool.h>
#include <stddef.h>

struct file;

/*
 * TODO: For now we only use a single static VMM.
 *       To allow for multiprocessing, which would require one VMM per process,
 *       we should either return a new VMM from the init function or ask for a
 *       pointer to a VMM instance as parameter.
 */

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

    node_t this; /*!< Used by processes to list the VMAs they currently own */

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

/* For mmap */
typedef enum mmap_flags {
    MAP_KERNEL = VM_KERNEL,
    MAP_CLEAR = VM_CLEAR,
} mmap_flags_t;

/**
 * Global kernel VMM, used to allocate shared kernel addresses.
 *
 * These addresses are stored in the PTEs above KERNEL_VIRTUAL_START, and are
 * shared across all processes. That is why we must use a global shared VMM.
 *
 * @todo TODO: This behaviour could be generalised once we implement MAP_SHARED
 */
extern vmm_t kernel_vmm;

/** Check whether a virtual address has been allocated using @ref kernel_vmm
 *  @ingroup vmm_internals
 */
#define IS_KERNEL_ADDRESS(_addr) \
    IN_RANGE((vaddr_t)(_addr), KERNEL_MEMORY_START, KERNEL_MEMORY_END)

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
 *
 * @note Is this a good design? Should I free the corresponding PTE and PF also?
 *       I guess we'll see later on when dealing with actual dynamic allocation
 *       using our heap allocator, or implementing munmap.
 */
void vmm_free(vmm_t *, vaddr_t, size_t length);

/**
 * @brief Find the VMA to which a virtual address belongs
 * @return The VMA containing this address, or NULL if not found
 */
struct vm_segment *vmm_find(vmm_t *, vaddr_t);

/** Release all the VMAs inside a VMM instance.
 *
 *  @warning This does not release the actual virtual addresses referenced
 *  by the VMAs, please make sure to release it at some point.
 */
void vmm_destroy(vmm_t *vmm);

/**
 * Create a new mapping in the virtual address space of the calling process.
 *
 * @param addr The starting address of the new mapping
 * @param length The length of the mapping (must be greater than 0)
 * @param prot Protection flags for the mapping.
 *             Must be a combination of @ref mmu_prot
 * @param flags Feature flags for the mapping (mapping, sharing, etc ...).
 *              Must be a combination of @ref vma_flags
 */
void *mmap(void *addr, size_t length, int prot, int flags);

/**
 * Map a file into memory
 *
 * @note TODO: This should be modified to take a file descriptor as parameter
 *             instead. it should also be merge together with mmap theoretically
 *             but I don't think mmap is destined to be kept inside the kernel's
 *             API so we'll see about that.
 *
 * @see mmap
 */
void *mmap_file(void *addr, size_t length, int prot, int flags, struct file *);

/**
 * Delete a mapping for the specified address range
 *
 * @param addr The starting address of the range
 * @param length The length of the range
 *
 * The starting address MUST be page aligned (EINVAL).
 *
 * @return A value of type @ref error_t
 *
 * @info Every page that the range is inside of will be unmaped, even if it's
 *       only for one byte. Beware of overflows and mind the alignment!
 */
int munmap(void *addr, size_t length);

#endif /* KERNEL_VMM_H */

/** @} */
