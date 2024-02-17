/** @file memory.h
 *
 *  Constants related to the kernel's memory layout.
 */

#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

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

#define KERNEL_IS_HIGHER_HALF

#define KERNEL_PHYSICAL_START 0x00100000
#define KERNEL_VIRTUAL_START 0xC0000000
#define KERNEL_MEMORY_OFFSET (KERNEL_VIRTUAL_START - KERNEL_PHYSICAL_START)

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
