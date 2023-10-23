#include <kernel/gdt.h>

void arch_setup(void)
{
    gdt_init();
}
