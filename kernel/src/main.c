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

    char a = 3;
    void *test = (void *)0xFF123445;

    log_err("main", "coucou: " LOG_FMT_16, (unsigned short)-1);
    log_warn("main", "coucou");
    log_dbg("main", "coucou");
    log_info("main", "coucou");

    log_variable(a);
    log_variable(test);
}
