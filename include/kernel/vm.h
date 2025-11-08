/**
 * @brief Virtual Memory System - Address Space
 *
 * # Address space
 *
 * Each process has an associated address space that defines the mapping between
 * An address space is composed of one or more memory segments, each managing a
 * contiguous region of virtual memory with common properties (such as backing
 * storage, protections, and fault handling).
 *
 * The address space API abstracts away the complexities of:
 * - Virtual-to-physical memory translation
 * - Page fault management
 * - Protection and permission settings
 *
 * This design is greatly inspired by that of SunOs (see docs/address_space.pdf)
 *
 * ## Considerations
 *
 * This API can only manage page-aligned addresses (and sizes). If you need to
 * align buffers smaller than a page, please use the @ref kmalloc API instead.
 * Note that the @ref kmalloc API always rounds up the requested size to a power
 * of two, so any allocation larger than a size could theoretically be done
 * using the VM API instead (but you'd have to manually align the buffer's size
 * to be page-aligned).
 *
 */

#ifndef KERNEL_VM_H
#define KERNEL_VM_H

#include <kernel/error.h>
#include <kernel/spinlock.h>

#include <libalgo/linked_list.h>
#include <utils/bits.h>

struct vmm;

/* We should not be modifying another process's address_space */
#define AS_ASSERT_OWNED(_as) \
    WARN_ON((_as) != &kernel_address_space && (_as) != current->process->as);

/** Address space */
struct address_space {
    // TODO: replace this lock with a R/W one.
    spinlock_t lock;   /*!< Address space wide lock. Functions that modify
                         the address space should take that lock. */
    struct vmm *vmm;   /*!< Used to allocate virtual memory segments */
    paddr_t mmu;       /*!< Used to map virtual addresses to physical memory */
    llist_t *segments; /*!< List of currently allocated segments */
    vaddr_t data_end;  /*!< End of the process's data segment */
    vaddr_t brk_end;   /*!< End of the process's brk segment */
};

/** @enum vm_flags */
typedef enum vm_flags {
    VM_NONE = 0,
    VM_EXEC = BIT(0),   /*!< Pages inside the area are executable */
    VM_READ = BIT(1),   /*!< Pages inside the area are readable */
    VM_WRITE = BIT(2),  /*!< Pages inside the area are writable */
    VM_KERNEL = BIT(3), /*!< Pages should only be accessible from kernel */
    VM_CLEAR = BIT(4),  /*!< Page content should be reset when allocating */
    VM_FIXED = BIT(5),  /*!< Start address in vm_alloc_at() is not a hint */
} vm_flags_t;

#define VM_KERNEL_RO (VM_KERNEL | VM_READ)
#define VM_KERNEL_WO (VM_KERNEL | VM_WRITE)
#define VM_KERNEL_RW (VM_KERNEL | VM_READ | VM_WRITE)

#define VM_USER_RO (VM_READ)
#define VM_USER_WO (VM_WRITE)
#define VM_USER_RW (VM_READ | VM_WRITE)

/** Segment driver
 *
 *  There exists different types of memory segments. A segment driver defines
 *  the operations that can be performed on a segment (allocating, freeing).
 */
struct vm_segment_driver {

    /** Allocate a segment of virtual memory.
     *
     *  @param as The address space this segment belongs to
     *  @param start If specified, the segment should start at this address
     *  @param size The size of the segment
     *
     *  @return The segment or a pointer encoded error
     *
     *  @see vm_alloc
     */
    struct vm_segment *(*vm_alloc)(struct address_space *, vaddr_t, size_t,
                                   vm_flags_t);

    /** Allocate a segment of virtual memory mapped to a physical address.
     *
     *  @param as The address space this segment belongs to
     *  @param start Start of the physical address range
     *  @param size The size of the segment
     *
     *  @note This function assumes that the physical address range
     *        is contiguous and large enough to contain @c size bytes.
     *
     *  @return The segment or a pointer encoded error
     *
     *  @see vm_alloc_at
     */
    struct vm_segment *(*vm_alloc_at)(struct address_space *, paddr_t, size_t,
                                      vm_flags_t);

    /** Free a contiguous virtual memory segment.
     *
     *  This function is also responsible for freeing the physical backing
     *  storage (pmm_free), and removing any eventual MMU entries.
     */
    void (*vm_free)(struct address_space *, struct vm_segment *);

    /** Resize a virtual memory segment.
     *
     *  When expanding, if the required virtual memory range has already
     *  been allocated, this function returns E_NOMEM.
     *
     *  Calling this function with a size of 0 is equivalent to vm_free().
     *
     *  @param size The new size of the segment
     */
    error_t (*vm_resize)(struct address_space *, struct vm_segment *, size_t);

    /** Handle a page fault exception on a known segment.
     *
     *  @param as The address space the segment belongs to
     *  @param segment The memory segment inside which the faulty address is
     *                 located.
     */
    error_t (*vm_fault)(struct address_space *, struct vm_segment *);
};

/** Kernel-only address-space.
 *
 * The content of this address space is shared accross all MMUs, so it is safe
 * to load it when in kernel mode. This is required when deleting a currently
 * loaded address space for example (see @ref process_free()).
 *
 * It is also used as the bootstrap address space, to be able to use the VM API
 * before having allocated any "real" address space. It is placed inside
 * the @ref kernel_process, and only replaced when calling @ref
 * process_init_kernel_process().
 */
extern struct address_space kernel_address_space;

/** Segment of contiguous virtual memory */
struct vm_segment {
    node_t this;   /*!< Used to enumerate allocated segments */
    vaddr_t start; /*!< Starting virtual address of this area */
    size_t size;   /*!< Size of the area */
    u32 flags;     /*!< A combination of @ref vm_flags */
    const struct vm_segment_driver *driver; /*!< Driver used to manipulate this
                                                 segment */
};

/** @return The end address of a contiguous virtual memory segment */
static inline vaddr_t segment_end(const struct vm_segment *segment)
{
    return segment->start + segment->size;
}

/** Allocate a new address space structure.
 *  The struct should be deleted using @ref address_space_destroy().
 */
struct address_space *address_space_new(void);

/** Initialize a new address space's content.
 *  Before deletion, the initialized content should be released using
 *  @ref address_space_clear()
 */
error_t address_space_init(struct address_space *);

/** Release all memory currently allocated inside an address space.
 *
 *  @warning: If you want to continue using the address space afterwards, you
 *  absolutely need to call address_space_init() on it again.
 */
error_t address_space_clear(struct address_space *);

/** Release the address space structure */
error_t address_space_destroy(struct address_space *);

/** Copy the current address space's content inside another.
 *
 * @note This function should ideally be modified to be able to copy any
 * arbitrary address space's content, but ut is currently only possible to clone
 * the currently loaded MMU. See comment above the mmu_clone() function for more
 * details.
 */
error_t address_space_copy_current(struct address_space *);

/** Change the currently used address space. */
error_t address_space_load(struct address_space *);

/** Handle a page fault exception.
 *
 * What the handler does:
 * * Lazy allocation of pageframes for allocated virtual addresses
 * * Duplication of CoW pages
 */
error_t address_space_fault(struct address_space *, void *, bool is_cow);

/** Allocate a buffer of virtual memory */
void *vm_alloc(struct address_space *, size_t, vm_flags_t);

/** Allocate a virtual memory buffer located after a given address
 *  @see vm_alloc
 */
void *vm_alloc_start(struct address_space *, void *, size_t, vm_flags_t);

/** Allocate a virtual memory buffer mapped to a given physical address
 *  @see vm_alloc
 */
void *vm_alloc_at(struct address_space *, paddr_t, size_t, vm_flags_t);

/** Free a buffer memory allocated inside an address space */
void vm_free(struct address_space *, void *);

/** Find the address space's segment that contains the given address */
struct vm_segment *vm_find(const struct address_space *, void *);

#endif /* KERNEL_VM_H */
