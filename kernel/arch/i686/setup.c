#include <kernel/i686/gdt.h>

void arch_setup(void)
{
    gdt_init();
    gdt_log();
}
