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

/**
 * @enum mmu_prot
 * @brief Protection flags passed to the mmu's functions
 */
typedef enum mmu_prot {
    PROT_NONE = 0x0, /*<! Pages may not be accessed */
    PROT_EXEC = 0x1, /*!< Pages may be executed */
    PROT_READ = 0x2, /*!< Pages may be read */
    // TODO: NX bit for PROT_EXEC
    PROT_WRITE = 0x4,  /*!< Pages may be written */
    PROT_KERNEL = 0x8, /*!< Pages should be accessible only from the kernel */
} mmu_prot;

/** Initialize the MMU's paging system
 *
 * This function is responsible for setting any required bit inside the CPU's
 * register.
 *
 * It is also responsible of remapping the kernel's code and addresses before
 * enabling paging.
 *
 * @warning  After calling this function, each and every address will
 * automatically be translated into its physical equivalent using the paging
 * mechanism. Be sure to remap known addresses to avoid raising exceptions.
 */
bool mmu_init(void);

/** @brief Inititialize a new page directory
 *  @return The physical address of the new page_directory, 0 if error.
 */
paddr_t mmu_new_page_directory(void);

/**
 * @brief Replace the current page directory.
 * @param page_directory The physical address of the page directory
 */
void mmu_load_page_directory(paddr_t);

/**
 * @brief Map a virtual address to a physical one
 *
 * @param virt The virtual address
 * @param physical Its physical equivalent
 * @param prot Protection rule in use for this page.
 *             A combination of @ref mmu_prot flags.
 *
 * @return False if the address was already mapped before
 */
bool mmu_map(vaddr_t virt, paddr_t physical, int prot);

/**
 * @brief Unmap a virtual address
 *
 * @warning After calling this, referencing the given virtual address may
 * cause the CPU to raise an exception.
 *
 * @param virt The virtual address
 *
 * @return The physical pageframe associated with the unmapped address
 */
paddr_t mmu_unmap(vaddr_t virt);

/** Unmap a range of virtual addresses
 *
 * @param start The start of the virtual address
 * @param end The end of the virtual address
 */
void mmu_unmap_range(vaddr_t start, vaddr_t end);

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
 * @param prot Protection rule in use for this page.
 *             A combination of @ref mmu_prot flags.
 */
void mmu_identity_map(paddr_t start, paddr_t end, int prot);

/** Find the physical mapping of a virtual address
 *  @return -E_INVAL if error, a physical address if none
 */
paddr_t mmu_find_physical(vaddr_t);

#endif /* KERNEL_MMU_H */
