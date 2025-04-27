#ifndef KERNEL_VM_H
#define KERNEL_VM_H

#include <utils/bits.h>

/***/
struct vm_segment {
    vaddr_t start; /*!< Starting virtual address of this area */
    size_t size;   /*!< Size of the area */
    u32 flags;     /*!< A combination of @ref vm_flags */
};

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

#endif /* KERNEL_VM_H */
