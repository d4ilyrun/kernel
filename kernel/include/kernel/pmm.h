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

#include <multiboot.h>
#include <stdbool.h>
#include <utils/types.h>

// The size of a single page
#define PAGE_SIZE (4096)

// 32-bit address bus -> 4GiB of addressable memory
#define ADDRESS_SPACE_SIZE (0x100000000UL)

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

/// @brief Address of the byte located just after the end of the kernel's code
///
/// Any byte written after this address will not overwrite our kernel's
/// executable binary.
///
/// @info this address is defined inside the kernel's linker scrpit.
extern u32 kernel_code_end_address;
#define KERNEL_CODE_END ((u32)&kernel_code_end_address)

/// @brief Address of the byte located just before the end of the kernel's code
///
/// Any byte written after this address **WILL** overwrite our kernel's
/// executable binary.
///
/// @info this address is defined inside the kernel's linker scrpit.
extern u32 kernel_code_start_address;
#define KERNEL_CODE_START ((u32)&kernel_code_start_address)

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
 * TODO: Allow for multiple page allocations (8092, 16384, ...)
 * TODO: Take pagetable settings as parameter (writable, user, ...)
 *
 * @return The pageframe's **physical** address, PMM_INVALID_PAGEFRAME on error
 */
u32 pmm_allocate(void);

/**
 * \brief Allocate a previously unused pageframe
 *
 * TODO: Allow for multiple pages (8092, 16384, ...)
 * TODO: Take pagetable settings as parameter (writable, user, ...)
 *
 * @return The pageframe's **physical** address
 */
void pmm_free(u32 pageframe);

#endif /* KERNEL_PMM_H */
