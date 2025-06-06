/**
 * @file kernel/arch/i686/memory.h
 *
 * @defgroup memory_x86 Memory Constants - x86
 * @ingroup memory
 * @ingroup x86
 *
 * Constants related to the kernel's memory layout on the x86 architecture
 *
 * @{
 */

#ifndef KERNEL_ARCH_I686_MEMORY_H
#define KERNEL_ARCH_I686_MEMORY_H

#ifndef KERNEL_MEMORY_H
#error <kernel/arch/i686/memory.h> must not be used as a standalone header. Please include <kernel/memory.h> instead.
#endif

/** Number of bits to shift one bit left to get the size of a page */
#define PAGE_SHIFT 12
/** The size of a single page */
#define PAGE_SIZE (1 << PAGE_SHIFT)

/** First usable physical address
 *
 * The first 256KiB of physical memory contain the BIOS' code and real mode
 * interrupt vectors table. It may still be used when switching back to real
 * mode so we should not overwrite it.
 */
#define PHYSICAL_MEMORY_START 0x500

/** Last usable physical address */
#define PHYSICAL_MEMORY_END 0xFFFFFFFF

/**
 * @brief The size of the virtual address space
 * 32-bit address bus -> 4GiB of addressable memory
 */
#define ADDRESS_SPACE_SIZE (0x100000000UL)
#define ADDRESS_SPACE_END (ADDRESS_SPACE_SIZE - 1)

#endif /* KERNEL_ARCH_I686_MEMORY_H */
