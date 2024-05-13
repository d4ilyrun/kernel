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

#include <kernel/types.h>

#include <libalgo/avl.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stddef.h>

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

    vaddr_t start; /*!< Starting virtual address of this area */
    size_t size;   /*!< Size of the area */
    u32 flags;     /*!< Feature flags, combination of @ref vma_flags */

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
static_assert(sizeof(vma_t) <= VMA_SIZE,
              "Update the allocated size for VMA structures!");

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

    /** Bitmap of the available virtual addreses inside the reserved area
     *
     *  TODO: Using a bitmap for this takes 2KiB of memory per VMM (so per
     * process)! Is there a less expensive way to keep track of them?
     */
    BITMAP(reserved, VMM_RESERVED_SIZE / VMA_SIZE);

} vmm_t;

/** Returned by VMM functions in case of error */
#define VMM_INVALID ((vaddr_t)NULL)

/** @enum vma_flags
 * @brief Feature flags for VMAs
 */
typedef enum vma_flags {
    VMA_NONE = 0,    /*!< Default */
    VMA_EXEC = 0x1,  /*!< Pages inside the area are executable */
    VMA_READ = 0x2,  /*!< Pages inside the area are readable */
    VMA_WRITE = 0x4, /*!< Pages inside the area are writable */
} vma_flags;

/**
 * @brief Initialize a VMM instance
 * @return Whether the init processes suceeded or not
 */
bool vmm_init(vaddr_t start, vaddr_t end);

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
 * @param addr Starting address for the allocated area.
 * @param size The size of the requested area
 * @param flags Feature flags used for the allocated area.
 *              Must be a combination of @link vma_flags @endlink
 *
 * @return The virtual start address of the area, or VMM_INVALID
 */
vaddr_t vmm_allocate(vaddr_t addr, size_t size, int flags);

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
void vmm_free(vaddr_t addr, size_t length);

/**
 * @brief Find the VMA to which a virtual address belongs
 * @return The VMA containing this address, or NULL if not found
 */
const vma_t *vmm_find(vaddr_t addr);

#endif /* KERNEL_VMM_H */

/** @} */
