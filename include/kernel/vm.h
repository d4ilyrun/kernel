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
 * This design is greatly inspired by that of SunOs (see
 * docs/address_space.pdf). We do not implement the segment_driver model, since
 * we currently have no use for it.
 *
 */

#ifndef KERNEL_VM_H
#define KERNEL_VM_H

#include <kernel/error.h>

#include <libalgo/linked_list.h>
#include <utils/bits.h>

struct vmm;
typedef llist_t kmalloc_t;

/***/
struct address_space {
    struct vmm *vmm;   /*!< Used to allocate virtual memory segments */
    paddr_t mmu;       /*!< Used to map virtual addresses to physical memory */
    llist_t segments;  /*!< List of currently allocated segments */
    kmalloc_t kmalloc; /*!< Opaque struct used by the memory allocator to
                          allocate memory blocks inside the user area */
};

extern struct address_space kernel_address_space;

/***/
struct vm_segment {
    node_t this;   /*!< Used to enumerate allocated segments */
    vaddr_t start; /*!< Starting virtual address of this area */
    size_t size;   /*!< Size of the area */
    u32 flags;     /*!< A combination of @ref vm_flags */
};

/***/
static inline vaddr_t segment_end(const struct vm_segment *segment)
{
    return segment->start + segment->size;
}

/** @enum vm_flags */
typedef enum vm_flags {
    VM_NONE = 0,
    VM_EXEC = BIT(0),   /*!< Pages inside the area are executable */
    VM_READ = BIT(1),   /*!< Pages inside the area are readable */
    VM_WRITE = BIT(2),  /*!< Pages inside the area are writable */
    VM_KERNEL = BIT(3), /*!< Should be mapped inside kernel pages */
    VM_CLEAR = BIT(4),  /*!< Page content should be reset when allocating */
} vm_flags;

/** Allocate a new address space structure.
 *  The struct should be deleted using @ref address_space_destroy().
 */
struct address_space *address_space_new(void);

/** Initialize a new address space's content.
 *  Before deletion, the initialized content should be released using
 *  @ref address_space_clear()
 */
error_t address_space_init(struct address_space *);

/** Release all memory currently allocated inside an address space */
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

/** Handle a page fault
 *
 * What the handler does:
 * * Lazy allocation of pageframes for allocated virtual addresses
 * * Duplication of CoW pages
 */
error_t address_space_fault(struct address_space *, void *, bool is_cow);

/** Allocate a buffer of virtual memory */
void *vm_alloc(struct address_space *, size_t, vm_flags);

/** Allocate a virtual memory buffer located after a given address
 *  @see vm_alloc
 */
void *vm_alloc_start(struct address_space *, void *, size_t, vm_flags);

/** Allocate a virtual memory buffer mapped to a given physical address
 *  @see vm_alloc
 */
void *vm_alloc_at(struct address_space *, paddr_t, size_t, vm_flags);

/** Free a buffer memory allocated inside an address space */
void vm_free(struct address_space *, void *, size_t);

/** Find the address space's segment that contains the given address */
struct vm_segment *vm_find(const struct address_space *, void *);

void vm_segment_insert(struct address_space *as, struct vm_segment *segment);
void vm_segment_remove(struct address_space *as, struct vm_segment *segment);

#endif /* KERNEL_VM_H */
