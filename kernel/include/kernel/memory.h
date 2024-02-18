/** @file memory.h
 *
 *  Constants related to the kernel's memory layout.
 */

#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE 0x4000
#endif

// NOTE: This maybe belong inside the arch directory ? (cf. #5)

// The size of a single page
#define PAGE_SIZE (4096)

// 32-bit address bus -> 4GiB of addressable memory
#define ADDRESS_SPACE_SIZE (0x100000000UL)
#define ADDRESS_SPACE_END (ADDRESS_SPACE_SIZE - 1)

/// Starting from #6, our kernel uses the higher-half design.
///
/// This means that the kernel code's virtual address differs from its physical
/// one. The following constants are used by the linker script and should be
/// used by the functions that depend on them instead of hardcoding values.

#define KERNEL_IS_HIGHER_HALF 1

// TODO: These values are duplicated inside the linkerscript
//       We should maybe find a way to include this header before linking the
//       kernel to avoid conflicts

#define KERNEL_PHYSICAL_START 0x00100000UL
#define KERNEL_HIGHER_HALF_OFFSET 0xC0000000UL
#define KERNEL_VIRTUAL_START (KERNEL_PHYSICAL_START + KERNEL_HIGHER_HALF_OFFSET)

#ifndef __ASSEMBLER__

#include <utils/types.h>

/// @brief Address of the byte located just before the end of the kernel's code
///
/// Any byte written after this address **WILL** overwrite our kernel's
/// executable binary.
///
/// @info this address is defined inside the kernel's linker scrpit.
extern u32 _kernel_code_start;
#define KERNEL_CODE_START ((u32)&_kernel_code_start)

/// @brief Address of the byte located just after the end of the kernel's code
///
/// Any byte written after this address will not overwrite our kernel's
/// executable binary.
///
/// @info this address is defined inside the kernel's linker scrpit.
extern u32 _kernel_code_end;
#define KERNEL_CODE_END ((u32)&_kernel_code_end)

#endif /* __ASSEMBLER__ */

#endif /* KERNEL_MEMORY_H */
