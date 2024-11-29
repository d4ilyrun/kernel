#ifndef KERNEL_DEVICES_UART_H
#define KERNEL_DEVICES_UART_H

#include <kernel/error.h>

/* TODO: Initcall */
error_t uart_init(void);

/* TODO: console use device */
int uart_putc(const char c);

#endif /* KERNEL_DEVICES_UART_H */
