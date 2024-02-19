/**
 * @brief Memory Management Unit
 *
 * Interact with the CPU's hardware MMU.
 *
 * The MMU is used to translate virtual addresses into physical ones.
 * These physical addresses should be pages, allocated using the PMM.
 * This whole translation process is called paging.
 *
 * All the functions defined inside this file are arch-dependant, and only
 * serve as a common interface for different architectures.
 *
 * This includes:
 * * Enabling/Disabling paging
 * * Updating the underlying structures
 *
 * @file mmu.h
 */

#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

#include <utils/types.h>

#include <stdbool.h>

/**
 * @brief Inititialize the MMU's underlying structures
 */
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
bool mmu_map(u32 virt, u32 physical);

/**
 * @brief Unmap a virtual address
 *
 * @warning After calling this, referencing the given virtual address may cause
 * the CPU to raise an exception.
 *
 * @param virt The virtual address
 */
void mmu_unmap(u32 virt);

/**
 * @brief Perform identity mapping inside a given virtual address range
 *
 * Identity mapping is the process of mapping a virtual address to the same
 * physical address.
 *
 * Both start and end addresses will be included inside the range.
 *
 * @brief start the starting page of the address range
 * @brief start the ending address of the address range
 */
void mmu_identity_map(u32 start, u32 end);

#endif /* KERNEL_MMU_H */
