#include <kernel/i686/interrupts.h>
#include <kernel/interrupts.h>

#include <string.h>
#include <utils/compiler.h>

void interrupts_disable(void)
{
    ASM("cli");
}

void interrupts_enable(void)
{
    ASM("sti");
}
