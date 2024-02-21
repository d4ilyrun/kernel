/**
 * @brief Physical Memory Manager
 *
 * The PMM is responsible for allocating and freeing new memory pages.
 * These pages are then used by the Virtual Memory Manager (e.g. UNIX's malloc)
 * to return (free) new mapped virtual addresses to the caller.
 *
 * The PMM should never interact with the virtual address space, this is the
 * responsabillity of the VMM only.
 *
 * @file pmm.h
 */

#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <kernel/memory.h>

#include <utils/macro.h>
#include <utils/types.h>

#include <multiboot.h>
#include <stdbool.h>

// Error value returned by the PMM in case of errors
#define PMM_INVALID_PAGEFRAME (0xFFFFFFFFUL)

// This is the theorical total number of pageframes available inside the whole
// address space.
//
// BUT, not all pageframes are necessarily available (usable for memory
// allocation). Some are reserved, used for ACPI, plus RAM is not guaranteed to
// be contiugous.
//
// This constant should ONLY be used as a compile-time known theoretical
// reference value (e.g. the physical memory manager's bit map size).
#define TOTAL_PAGEFRAMES_COUNT (ADDRESS_SPACE_SIZE / PAGE_SIZE)

/// \defgroup pmm_allocation_flags
///
/// Flags used when allocating a page frame to specify that the allocation must
/// respect certain constraints. Constraints can be specific addresses, rules,
/// etc...
///
/// @{

/// The pageframe should be located inside the kernel's physical address space.
#define PMM_MAP_KERNEL_BIT 0x1
#define PMM_MAP_KERNEL BIT(PMM_MAP_KERNEL_BIT)

/// @}

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
 * \brief Allocate a previously unused pageframe
 *
 * @return The pageframe's **physical** address, PMM_INVALID_PAGEFRAME on error
 */
u32 pmm_allocate(int flags);

/**
 * \brief Allocate a previously unused pageframe
 *
 * TODO: Take pagetable settings as parameter (writable, user, ...)
 *
 * @return The pageframe's **physical** address
 */
void pmm_free(u32 pageframe);

#endif /* KERNEL_PMM_H */
