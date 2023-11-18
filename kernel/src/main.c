#include <kernel/devices/pic.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>

#include "utils/compiler.h"

void arch_setup(void);

void kernel_main(void)
{
    pic_reset();
    uart_reset();
    tty_init();
    interrupts_init();
    arch_setup();

    pic_disable_irq(IRQ_COM1);
    pic_disable_irq(IRQ_COM2);

    interrupts_enable();

    ASM("int $0");
}
