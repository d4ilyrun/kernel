/** @file memory.h
 *
 *  Constants related to the kernel's memory layout.
 */

#ifndef KERNEL_ARCH_I686_MEMORY_H
#define KERNEL_ARCH_I686_MEMORY_H

#ifndef KERNEL_MEMORY_H
#error <kernel/i686/memory.h> must not be used as a standalone header. Please include <kernel/memory.h> instead.
#endif

// The size of a single page
#define PAGE_SIZE (4096)

// 32-bit address bus -> 4GiB of addressable memory
#define ADDRESS_SPACE_SIZE (0x100000000UL)
#define ADDRESS_SPACE_END (ADDRESS_SPACE_SIZE - 1)

#endif /* KERNEL_ARCH_I686_MEMORY_H */
