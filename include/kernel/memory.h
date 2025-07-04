/**
 * @file kernel/memory.h
 *
 * @defgroup memory Memory Constants
 * @ingroup kernel
 *
 * # Memory Constants
 *
 * Constants related to the kernel's memory layout.
 *
 * ## Virtual address space layout
 *
 *      0xFFFF_FFFF --------------------
 *                  |                  |
 *                  |    Page Tables   |
 *      0xFFC0_0000 |------------------|
 *                  |      Kernel      |
 *                  |   VMM Reserved   |
 *      0xFFB0_0000 |------------------|
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *                  |  Kernel Memory   |
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *      0xC000_0000 |------------------|
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *                  |       ...        |
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *                  |                  |
 *      0x0010_0000 |------------------|
 *                  |   VMM Reserved   |
 *      0x0001_0000 --------------------
 *                  |     NULL Page    |
 *      0x0000_0000 --------------------
 *
 * @{
 */

#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#if ARCH == i686
#include <kernel/arch/i686/memory.h>
#endif

#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE 0x4000U /*!< Size of the kernel's stack */
#endif

#ifndef USER_STACK_SIZE
#define USER_STACK_SIZE 0x10000U /*!< Size of the user stack */
#endif

#ifndef __ASSEMBLER__

#include <utils/compiler.h>
#include <utils/math.h>

/* We want to allocate stacks using the VM API, which can only allocate pages */
static_assert(is_aligned(KERNEL_STACK_SIZE, PAGE_SIZE));
static_assert(is_aligned(USER_STACK_SIZE, PAGE_SIZE));

#endif

/* Starting from #6, our kernel uses the higher-half design.
 *
 * This means that the kernel code's virtual address differs from its physical
 * one. The following constants are used by the linker script and should be
 * used by the functions that depend on them instead of hardcoding values.
 */

#define KERNEL_IS_HIGHER_HALF 1

// TODO: These values are duplicated inside the linkerscript
//       We should maybe find a way to include this header before linking the
//       kernel to avoid conflicts

/** Physical address our kernel is loaded at */
#define KERNEL_PHYSICAL_START 0x00100000UL
/** Offset used fo relocation when linking our kernel */
#define KERNEL_HIGHER_HALF_OFFSET 0xC0000000UL
/** Virtual address our kernel is linked at */
#define KERNEL_VIRTUAL_START (KERNEL_PHYSICAL_START + KERNEL_HIGHER_HALF_OFFSET)

#ifdef __ASSEMBLER__

#define KERNEL_HIGHER_HALF_PHYSICAL(_virtual) \
    ((_virtual) - KERNEL_HIGHER_HALF_OFFSET)
#define KERNEL_HIGHER_HALF_VIRTUAL(_physical) \
    ((_physical) + KERNEL_HIGHER_HALF_OFFSET)

#else

/** Compute the physical equivalent of a higher half relocated virtual address
 */
#define KERNEL_HIGHER_HALF_PHYSICAL(_virtual) \
    ((u32)(_virtual) - KERNEL_HIGHER_HALF_OFFSET)
/** Compute the higher half virtual equivalent of a physical address */
#define KERNEL_HIGHER_HALF_VIRTUAL(_physical) \
    ((u32)(_physical) + KERNEL_HIGHER_HALF_OFFSET)

#include <kernel/types.h>

/**
 * @brief Address of the byte located just before the end of the kernel's code
 *
 * Any byte written after this address **WILL** overwrite our kernel's
 * executable binary.
 *
 * @note this address is defined inside the kernel's linker scrpit.
 */
extern u32 _kernel_code_start;
#define KERNEL_CODE_START ((u32) & _kernel_code_start)

/**
 * @brief Address of the byte located just after the end of the kernel's code
 *
 * Any byte written after this address will not overwrite our kernel's
 * executable binary.
 *
 * @note this address is defined inside the kernel's linker scrpit.
 */
extern u32 _kernel_code_end;
#define KERNEL_CODE_END ((u32) & _kernel_code_end)

#endif /* __ASSEMBLER__ */

#define PAGE_TABLES_START (0xFFC00000)

/** @brief Location of the reserved range for the kernel's VMM structure
 *  @ref kernel_vmm
 *  @{
 */
#define KERNEL_VMM_RESERVED_END (PAGE_TABLES_START)
#define KERNEL_VMM_RESERVED_START (KERNEL_VMM_RESERVED_END - VMM_RESERVED_SIZE)
/** @} */

#define KERNEL_MEMORY_END (KERNEL_VMM_RESERVED_START)
#define KERNEL_MEMORY_START (KERNEL_CODE_END)

#define USER_MEMORY_END KERNEL_VIRTUAL_START
#define USER_MEMORY_START VMM_RESERVED_END

/**
 * @brief Size of the area reserved for allocating memory management structures
 *
 * ~1MiB Virtual memory range reserved for allocating VMA structures.
 *
 * @see @ref vmm
 */
#define VMM_RESERVED_END 0x100000UL
#define VMM_RESERVED_START NULL_PAGE_END
#define VMM_RESERVED_SIZE (VMM_RESERVED_END + VMM_RESERVED_START)

/** @brief A single page of memory reserved to detect NULL references. */
#define NULL_PAGE_SIZE PAGE_SIZE
#define NULL_PAGE_START 0x0
#define NULL_PAGE_END (NULL_PAGE_START + NULL_PAGE_SIZE)

#define PAGE_ALIGNED(_ptr) is_aligned_ptr(_ptr, PAGE_SIZE)

#endif /* KERNEL_MEMORY_H */
