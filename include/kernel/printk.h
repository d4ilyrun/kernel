#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <utils/compiler.h>

#include <stdarg.h>

int printk(const char *restrict format, ...) FORMAT(printf, 1, 2);
int vprintk(const char *restrict format, va_list parameters);

#endif /* KERNEL_PRINTK_H */
