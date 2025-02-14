/**
 * @file kernel/pmm.h
 *
 * @defgroup PMM Physical Memory Manager
 * @ingroup kernel
 *
 * # Physical Memory Manager
 *
 * The PMM is responsible for allocating and freeing new memory pageframes.
 * These pages are then used by the Virtual Memory Manager (e.g. UNIX's mmap)
 * to return new (or free) mapped virtual addresses to the caller.
 *
 * The PMM should never interact with the virtual address space, this is the
 * responsabillity of the VMM only.
 *
 * ## Desig,
 *
 * @{
 */

#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <kernel/memory.h>
#include <kernel/types.h>

#include <utils/bits.h>

#include <multiboot.h>
#include <stdbool.h>

/** Error value returned by the PMM in case of errors */
#define PMM_INVALID_PAGEFRAME (0xFFFFFFFFUL)

/**
 * @brief Total number of pageframes
 *
 * This is the **theorical** total number of pageframes available inside the
 * whole address space.
 *
 * **BUT**, not all pageframes are necessarily available (usable for memory
 * allocation). Some are reserved, used for ACPI, plus RAM is not guaranteed to
 * be contiugous.
 *
 * This constant should **ONLY** be used as a compile-time known theoretical
 * reference value (e.g. the physical memory manager's bit map size).
 */
#define TOTAL_PAGEFRAMES_COUNT (ADDRESS_SPACE_SIZE / PAGE_SIZE)

/**
 * \defgroup Flags PMM Allocation Flags
 *
 * Flags used when allocating a page frame to specify that the allocation must
 * respect certain constraints. Constraints can be specific addresses, rules,
 * etc...
 *
 * @{
 */

typedef enum pmm_flags {
    /** Pageframe should be located inside the kernel physical address space */
    PMM_MAP_KERNEL = 0x1,
} pmm_flags;

/** @} */

/**
 * @brief Initialize the Physical Memory Mapper
 *
 * * Identify all the available page frames
 * * Locate the first page frame
 *
 * The list of available page frames is retrieved from the memory map
 * passed by our multiboot compliant bootloader.
 *
 * @param multiboot_info The information struct passed by the
 * multiboot-compliant bootloader to our entry function
 *
 * @warning This function should be called only once when starting the kernel.
 * Otherwise it will overwrite the content of the underlying structures.
 */
bool pmm_init(struct multiboot_info *);

/**
 * @brief Allocate previously unused pageframes
 *
 * @info The returned pageframes are guaranteed to be contiguous
 *
 * @param size The size of the area to allocate (must me a multiple of
 * PAGE_SIZE)
 * @param flags Allocation flags
 *
 * @return The pageframe range's **physical** address, PMM_INVALID_PAGEFRAME on
 * error
 */
paddr_t pmm_allocate_pages(size_t size, int flags);

/** Release previously allocated contiguous pageframes */
void pmm_free_pages(paddr_t pageframe, size_t size);

/**
 * @brief Allocate a previously unused pageframe
 * @return The pageframe's **physical** address, PMM_INVALID_PAGEFRAME on error
 */
#define pmm_allocate(flags) pmm_allocate_pages(PAGE_SIZE, flags)

#define pmm_free(pageframe) pmm_free_pages(pageframe, PAGE_SIZE)

#endif /* KERNEL_PMM_H */

/** @} */
