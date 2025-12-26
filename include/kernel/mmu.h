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
 * TODO: It should ideally be possible to modify the content of another MMU.
 * We have no efficient way of accessing an arbitrary physical address without
 * having to go through the VMM to find an address.
 *
 * @{
 */

#ifndef KERNEL_MMU_H
#define KERNEL_MMU_H

#include <kernel/error.h>
#include <kernel/types.h>

#include <utils/bits.h>

#include <stdbool.h>

/**
 * @enum mmu_prot
 * @brief Protection flags passed to the mmu's functions
 */
typedef enum mmu_prot {
    PROT_NONE = 0x0, /*!< Pages may not be accessed */
    PROT_EXEC = 0x1, /*!< Pages may be executed */
    PROT_READ = 0x2, /*!< Pages may be read */
    // TODO: NX bit for PROT_EXEC
    PROT_WRITE = 0x4,  /*!< Pages may be written */
    PROT_KERNEL = 0x8, /*!< Pages should be accessible only from the kernel */
} mmu_prot;

/** @enum mmu_caching_policy
 *  @brief Caching policies.
 */
typedef enum mmu_caching_policy {
    POLICY_UC = BIT(6), /*!< Uncachealbe memory. */
    POLICY_WC = BIT(7), /*!< Write-combining memory. */
    POLICY_WT = BIT(8), /*!< Write-through memory. */
    POLICY_WB = BIT(9), /*!< Write-back memory. */
} mmu_policy_t;

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

/** @brief Allocate and initialize a new page directory
 *  @return The physical address of the new page_directory, 0 if error.
 */
paddr_t mmu_new(void);

/** Release the MMU's page_directory.
 *  @note This function does not release all the memory that was potentially
 *  mapped by the MMU. This should be done separately by the caller.
 */
void mmu_destroy(paddr_t mmu);

/** Clone the current MMU inside another one */
void mmu_clone(paddr_t destination);

/** Try to remap a potential copy-on-write mapping */
error_t mmu_copy_on_write(vaddr_t);

/**
 * @brief Replace the current page directory.
 * @param page_directory The physical address of the page directory
 */
void mmu_load(paddr_t mmu);

/**
 * @brief Map a virtual address to a physical one
 *
 * @param virt The virtual address
 * @param physical Its physical equivalent
 * @param prot Protection rule in use for this page.
 *             A combination of @ref mmu_prot and @ref mmu_caching_policy flags.
 *
 * @return False if the address was already mapped before
 */
bool mmu_map(vaddr_t virt, paddr_t physical, int prot);

/**
 * @brief Map a range of virtual addresses to physical ones
 *
 * @param virt The start of the virtual address range
 * @param physical Its physical equivalent
 * @param size The size of the region to map
 * @param prot Protection rule in use for this page.
 *             A combination of @ref mmu_prot and @ref mmu_caching_policy flags.
 *
 * @return False if the address was already mapped before
 */
bool mmu_map_range(vaddr_t virt, paddr_t physical, size_t size, int prot);

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
 *             A combination of @ref mmu_prot and @ref mmu_caching_policy flags.
 */
void mmu_identity_map(paddr_t start, paddr_t end, int prot);

/** Find the physical mapping of a virtual address
 *  @return -E_INVAL if error, a physical address if none
 */
paddr_t mmu_find_physical(vaddr_t);

/** @return Whether the current MMU contains a mapping for a virtual address. */
static inline bool mmu_is_mapped(vaddr_t addr)
{
    return !IS_ERR(mmu_find_physical(addr));
}

/** Configure the caching policy in effect when accessing a page.
 *
 * @param vaddr   The page's virtual address
 * @param policy  Caching policy applied to this page.
 *                A combination of @ref mmu_caching_policy flags.
 */
error_t mmu_set_policy(vaddr_t, mmu_policy_t policy);

/** Configure the caching policy in effect when accessing a range of pages.
 *
 * @param start   The virtual address of the first page in the address range
 * @param size    The size of the address range
 * @param policy  Caching policy applied to this page.
 *                A combination of @ref mmu_caching_policy flags.
 */
error_t mmu_set_policy_range(vaddr_t range_start, size_t range_size,
                             mmu_policy_t policy);

#endif /* KERNEL_MMU_H */
