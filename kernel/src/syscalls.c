#include <kernel/devices/uart.h>
#include <kernel/syscalls.h>

int write(const char *buf, size_t count)
{
    return uart_write(buf, count);
}

int read(void *buf, size_t count)
{
    return uart_read(buf, count);
}
