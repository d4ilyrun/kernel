/** @file memory.h
 *
 *  Constants related to the kernel's memory layout.
 */

#ifndef KERNEL_MEMORY_H
#define KERNEL_MEMORY_H

#if ARCH == i686
#include <kernel/i686/memory.h>
#endif

#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE 0x4000
#endif

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

#ifdef __ASSEMBLER__

#define KERNEL_HIGHER_HALF_PHYSICAL(_virtual) \
    ((_virtual)-KERNEL_HIGHER_HALF_OFFSET)
#define KERNEL_HIGHER_HALF_VIRTUAL(_physical) \
    ((_physical) + KERNEL_HIGHER_HALF_OFFSET)

#else

#define KERNEL_HIGHER_HALF_PHYSICAL(_virtual) \
    ((u32)(_virtual)-KERNEL_HIGHER_HALF_OFFSET)
#define KERNEL_HIGHER_HALF_VIRTUAL(_physical) \
    ((u32)(_physical) + KERNEL_HIGHER_HALF_OFFSET)

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

//         Virtual Address Space Layout
//
// 0xFFFF_FFFF --------------------
//             |                  |
//             |  Kernel Memory   |
//             |                  |
// 0xC000_0000 |------------------|
//             |                  |
//             |                  |
//             |       ...        |
//             |                  |
//             |                  |
// 0x0110_0000 |------------------|
//             |  VMM Reserved    |
// 0x0100_0000 |------------------|
//             |     Reserved     |
// 0x0000_0000 |------------------|

/// 1MiB Virtual memory range reserved for allocating VMA structures.
/// We place this area first thing first thing after our bootstrap code.
/// See vmm.h for more information
#define VMM_RESERVED_SIZE 0x100000
#define VMM_RESERVED_START KERNEL_HIGHER_HALF_PHYSICAL(KERNEL_CODE_START)
#define VMM_RESERVED_END (VMM_RESERVED_START + VMM_RESERVED_SIZE)

#endif /* KERNEL_MEMORY_H */
