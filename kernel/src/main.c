#include <kernel/devices/pic.h>
#include <kernel/devices/uart.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>
#include <kernel/terminal.h>

void arch_setup(void);

void kernel_main(void)
{
    pic_reset();
    uart_reset();
    tty_init();
    arch_setup();

    log_err("main", "Hello");
    log_warn("main", "Hello");
    log_dbg("main", "Hello");
    log_info("main", "Hello");
    write("Hello, UART!", sizeof("Hello, UART!"));
}
