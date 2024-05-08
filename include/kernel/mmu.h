/**
 * @file kernel/mmu.h
 *
 * @defgroup mmu Memory Management Unit
 * @ingroup kernel
 *
 * # Memory Managerment Unit (MMU)
 *
 * Interface with the CPU's hardware MMU.
 *
 * The MMU automatically translates virtual addresses into physical ones.
 * These physical addresses should be pages, allocated using the PMM.
 * This whole translation process is called paging.
 *
 * This includes:
 * * Enabling/Disabling paging
 * * Updating the underlying structures
 *
 * @{
 */

#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

#include <kernel/types.h>

#include <stdbool.h>

/** @brief Inititialize the MMU's underlying structures */
bool mmu_init(void);

/**
 * @brief Enable paging and automatic virtual address translation.
 *
 * @warning After calling this function, each and every address will
 * automatically be translated into its physical equivalent using the paging
 * mechanism. Be sure to remap known addresses to avoid raising exceptions.
 */
bool mmu_start_paging(void);

/**
 * @brief Map a virtual address to a physical one
 *
 * @param virt The virtual address
 * @param physical Its physical equivalent
 *
 * @return False if the address was already mapped before
 */
bool mmu_map(vaddr_t virt, paddr_t physical);

/**
 * @brief Unmap a virtual address
 *
 * @warning After calling this, referencing the given virtual address may cause
 * the CPU to raise an exception.
 *
 * @param virt The virtual address
 *
 * @return The physical pageframe associated with the unmapped address
 */
paddr_t mmu_unmap(vaddr_t virt);

/**
 * @brief Perform identity mapping inside a given virtual address range
 *
 * Identity mapping is the process of mapping a virtual address to the same
 * physical address.
 *
 * Both start and end addresses will be included inside the range.
 *
 * @param start the starting page of the address range
 * @param end the ending address of the address range
 */
void mmu_identity_map(paddr_t start, paddr_t end);

#endif /* KERNEL_MMU_H */
