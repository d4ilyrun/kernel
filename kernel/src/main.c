#include <kernel/terminal.h>

void kernel_main(void)
{
    tty_init();
    tty_puts("Hello, World");
}
