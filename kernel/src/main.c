#include <kernel/devices/pic.h>
#include <kernel/devices/uart.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>

void arch_setup(void);

void kernel_main(void)
{
    pic_reset();
    uart_reset();
    tty_init();
    interrupts_init();
    arch_setup();

    interrupts_enable();

    ASM("int $0");
}
