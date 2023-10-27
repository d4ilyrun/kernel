#include <kernel/interrupt.h>

#include <utils/compiler.h>

void interrupt_disable(void)
{
    ASM("cli");
}

void interrupt_enable(void)
{
    ASM("sti");
}
