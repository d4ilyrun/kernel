#include <kernel/memory.h>

#include <multiboot.h>

/*
 * Define the multiboot_header, following the GNU multiboot specifications,
 * this will allow for all bootloaders compatible with multiboot to detect
 * and identify our kernel as such.
 */

#define MULTIBOOT_HEADER_SIZE (.multiboot_header_end - .multiboot_header_start)

.section .multiboot
.align MULTIBOOT_HEADER_ALIGN
.multiboot_header_start:

.long MULTIBOOT2_HEADER_MAGIC
.long MULTIBOOT_ARCHITECTURE_I386
.long MULTIBOOT_HEADER_SIZE
.long -(MULTIBOOT_HEADER_SIZE + MULTIBOOT_ARCHITECTURE_I386 + MULTIBOOT2_HEADER_MAGIC)

#define MULTIBOOT_TAG(type, flags, size) \
    .align MULTIBOOT_TAG_ALIGN ; \
    .word MULTIBOOT_HEADER_TAG_ ## type ; \
    .word flags ; \
    .long size ; \

#define MULTIBOOT_INFO_REQUEST(type) \
    .long MULTIBOOT_TAG_TYPE_ ## type

MULTIBOOT_TAG(INFORMATION_REQUEST, 0, 20)
MULTIBOOT_INFO_REQUEST(MMAP)
MULTIBOOT_INFO_REQUEST(MODULE)
MULTIBOOT_INFO_REQUEST(ACPI_NEW)

MULTIBOOT_TAG(MODULE_ALIGN, 0, 8)
MULTIBOOT_TAG(END, 0, 8)

.multiboot_header_end:

/*
 * The stack, required by C functions and not defined by default.
 * Should be placed inside the stack pointer register (esp) inside
 * the start function.
 *
 * The ABI requires the stack to be 16-byte aligned.
 */

.section .bss
.align 16
startup_kernel_stack_bottom:
    .skip KERNEL_STACK_SIZE
startup_kernel_stack_top:

/*
 * Temporary paging structure used by the startup code to temporarily activate paging.

 * This is needed as we use a higher-half kernel design, meaning the kernel's code
 * has been virtually relocated into the higher-half of the address space by the linker.
 *
 * Refer to #6 for more info.
 *
 * WARNING: Each and every block of 4MiB inside the address space are mapped to the first 1024 pageframes.
 *          This only works if the kernel is smaller than 3MiB in size.
 */

.section .data
.align PAGE_SIZE
startup_page_directory:
    .rept 1024
    .long KERNEL_HIGHER_HALF_PHYSICAL(startup_page_table) + 1
    .endr
startup_page_table:
    .set addr, 0
    .rept 1024
    .long addr | 3
    .set addr, addr + 0x1000
    .endr

/*
 * The entry point of our kernel (as defined in our linker script).
 *
 * This is the function called by the bootloader after detection the
 * previously defined multiboot header.
 *
 * Once we enter this function, we are in 32-bit protected mode, no interrupts,
 * no paging, and complete control over the hardware. The wild wild west ...
 */

.section .text.startup
.global _kernel_start
.type _kernel_start, @function

_kernel_start:
    // install temporary bootstrap paging structure
    movl $KERNEL_HIGHER_HALF_PHYSICAL(startup_page_directory), %ecx
    movl %ecx, %cr3
    // activate paging
    movl %cr0, %ecx
    orl $0x80000000, %ecx
    movl %ecx, %cr0
    // jump to higher half
    lea higher_half, %ecx
    jmp *%ecx

.size _kernel_start, . - _kernel_start

.extern kernel_startup_process

.section .text
higher_half:

    // Starting from here we are running in higher-half !

    // Initialize stack pointer
    movl $startup_kernel_stack_top, %esp
    // Store it into the startup's task ESP0 (required for multitasking)
    leal (kernel_startup_process), %edx
    movl $startup_kernel_stack_top, 0x4(%edx)

    // The bootloader stores information about our system (memory map, ...)
    // inside these registers. We push them onto the stack to be able to access them
    // as arguments of our main function.
    addl $KERNEL_HIGHER_HALF_OFFSET, %ebx
    push %eax
    push %ebx

    // Enter the higher-level kernel code
    call kernel_main

    // If we exit from the kernel, loop infinitely.

    cli
_kernel_exit_loop:
    hlt
    jmp _kernel_exit_loop

.size higher_half, . - higher_half
