#include <kernel/i686/devices/pic.h>
#include <kernel/i686/gdt.h>
#include <kernel/interrupts.h>

void arch_setup(void)
{
    gdt_init();
    gdt_log();

    interrupts_init();
    idt_log();

    pic_reset();
}
