#include <kernel/devices/pic.h>
#include <kernel/devices/uart.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>

void kernel_main(void)
{
    pic_reset();
    uart_reset();

    tty_init();
    tty_puts("Hello, World");
    write("Hello, UART!", sizeof("Hello, UART!"));
}
