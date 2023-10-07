#include <kernel/terminal.h>
#include <kernel/devices/pic.h>

void kernel_main(void)
{
    pic_reset();

    tty_init();
    tty_puts("Hello, World");
}
