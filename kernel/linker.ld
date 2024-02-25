/*
 * The entry point of our kernel.
 * The bootloader will jump to this symbol to boot our kernel.
 */
ENTRY(_kernel_start)

_kernel_physical_start = 0x00100000;
_kernel_higher_half_offset =  0xC0000000;
_kernel_virtual_start = (_kernel_physical_start + _kernel_higher_half_offset);

SECTIONS
{
    /*
     * Starting from #6 we now use a higher-half kernel.
     *
     * The load address of the kernel is different from the relocation address (AT).
     * We run the code as if in higher-half, but is should in fact be loaded at the
     * beginning of the RAM to be detected by the bootloader.
     *
     * We still need to run the piece of code that jumps to our main function,
     * that is why we have 2 different sections for the code:
     * * .text.startup: should be identically mapped (load address == relocation address)
     * * .text: should be loaded as higher half
     *
     * The code inside the .text.startup section can be discarded once we jumped to our
     * main C function, we shall never get back to it anyway.
     *
     * For more information, refer to: https://wiki.osdev.org/Higher_Half_x86_Bare_Bones
     */

    /* We place our kernel at 1MiB, it's a conventional place for it to be here. */
    . = ALIGN(4K);
    . = _kernel_physical_start;

    /* Code needed to bootstrap our kernel, cannot be remmaped */
    .startup ALIGN(4K) :
    {
        KEEP(*(.multiboot))
        *(.text.startup)
    }

    /* Everything after this is relocated to the end of the address space (> 3GiB) and needs paging to be activated */

    . = ALIGN(4K);
    . += _kernel_higher_half_offset;

    _kernel_code_start = .;

    .text ALIGN(4K) : AT (ADDR (.text) - _kernel_higher_half_offset)
    {
        *(.text)
    }

    /* Read-only data */
    .rodata ALIGN(4K) : AT (ADDR (.rodata) - _kernel_higher_half_offset)
    {
        *(.rodata)
    }

    /* Initialized data (RW) */
    .data ALIGN(4K) : AT (ADDR (.data) - _kernel_higher_half_offset)
    {
        *(.data)
    }

    /* Uninitialized data and stack (RW) */
    .bss ALIGN(4K) : AT (ADDR (.bss) - _kernel_higher_half_offset)
    {
        *(COMMON)
        *(.bss)
    }

    /* Kernel symbol table. Should be inserted during post-processing by scripts/extract_kernel_symbols.py */
    .kernel_symbols ALIGN(4K) : AT (ADDR (.kernel_symbols) - _kernel_higher_half_offset)
    {
        *(.kernel_symbols)
    }

    _kernel_code_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
    }
}
