#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <stdarg.h>

int printf(const char *restrict format, ...);
int vprintf(const char *restrict format, va_list parameters);

#endif /* LIBC_STDIO_H */
