/**
 * @brief Physical Memory Manager
 *
 * The PMM is responsible for allocating and freeing new memory pages.
 * These pages are then used by the Virtual Memory Manager (e.g. UNIX's malloc)
 * to return (free) new valid pointers to the caller.
 *
 * These pointers are isolated between different tasks. Meaning a same address
 * on two different tasks will not translate to the same physical address.
 *
 * @info The pages returned by this PMM all use the 32-bit paging mode. This
 * mode maps a 32-bits linear adress to a 40-bit physical address range.
 *
 * @file pmm.h
 */

#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <utils/types.h>

// The size of a single page
#define PAGE_SIZE 4096

typedef __attribute__((__aligned__(PAGE_SIZE))) void *ppm_page_frame;

// TODO: Use variables passed from the bootloader to determine the first
// available page frame.

/// @brief Address of the byte located just after the end of the kernel's code
///
/// Any byte written after this address will not overwrite our kernel's
/// executable binary.
///
/// @info this address is defined inside the kernel's linker scrpit.
extern u32 kernel_code_end_address;
#define KERNEL_CODE_END() &kernel_code_end_address;

/**
 * @brief Initialize the Pshydical Memory Pager
 *
 * * Locate the first addressable page frame
 * * Initialize the page directory and page tables.
 * * Set the content of the CR3 register
 *
 * @todo Should this function be responsible for setting the content of the CR3
 * register?
 *
 * @warning This function should be called only once when starting the kernel.
 * Otherwise it will overwrite the content of the underlying structures.
 */
void pmm_init(void);

#endif /* KERNEL_PMM_H */
