#ifndef KERNEL_DEVICES_UART_H
#define KERNEL_DEVICES_UART_H

#include <stddef.h>

void uart_reset();

int uart_putc(const char c);
int uart_write(const char *buf, size_t length);

char uart_getc();
size_t uart_read(char *buf, size_t length);

#endif /* KERNEL_DEVICES_UART_H */
