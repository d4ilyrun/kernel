#ifndef KERNEL_PRINTK_H
#define KERNEL_PRINTK_H

#include <utils/compiler.h>

#include <stdarg.h>
#include <stddef.h>

int printk(const char *restrict format, ...) FORMAT(printf, 1, 2);
int vprintk(const char *restrict format, va_list parameters);

/* Write formatted string into a buffer.
 *
 * A NUL character is ALWAYS written at the end of the buffer (i.e the last
 * character of a buffsiz long result string will be trimmed).
 *
 * @return The number of bytes that were written
 */
int snprintk(char *buffer, size_t buffsize, const char *restrict format, ...)
    FORMAT(printf, 3, 4);

#endif /* KERNEL_PRINTK_H */
