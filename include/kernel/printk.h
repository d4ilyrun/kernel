#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <stdarg.h>

int printk(const char *restrict format, ...);
int vprintk(const char *restrict format, va_list parameters);

#endif /* KERNEL_PRINTK_H */
