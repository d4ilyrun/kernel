#include <kernel/interrupts.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/gdt.h>

void arch_setup(void)
{
    gdt_init();
    gdt_log();
    interrupts_init();
    pic_reset();
}
