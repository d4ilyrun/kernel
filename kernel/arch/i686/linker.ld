/*
 * The entry point of our kernel.
 * The bootloader will jump to this symbol to boot our kernel.
 */
ENTRY(_kernel_start)

SECTIONS
{
    /* We place our kernel at 1MiB, it's a conventional place for it to be here. */
    . = 1M;

    _kernel_code_start = .;

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    /* Read-only data */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    /* Initialized data (RW) */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    /* Uninitialized data and stack (RW) */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    _kernel_code_end = .;
}
