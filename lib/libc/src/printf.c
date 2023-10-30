#include <kernel/devices/uart.h>

#include <stdarg.h>
#include <stdio.h>
#include <utils/compiler.h>

static_assert(sizeof(int) == sizeof(long),
              "Invalid architecture: sizeof(long) != sizeof(int)");

#define TOK_DELIMITER '%'

// supported tokens
#define TOK_STR 's'
#define TOK_ASCII 'c'
#define TOK_DECIMAL 'd'
#define TOK_HEX 'x'
#define TOK_OCTAL 'o'
#define TOK_UNSIGNED 'u'
#define TOK_POINTER 'p'
#define TOK_BINARY 'b' // custom token
#define TOK_ESCAPE TOK_DELIMITER

#define MAXBUF (sizeof(long) * 8) // 32B max (times 8 bits)

static inline void __attribute__((always_inline))
printf_char(register char c, int *written)
{
    uart_putc(c);
    *written += 1;
}

// NOLINTNEXTLINE(misc-no-recursion)
static void printf_puts(register char *str, int *written)
{
    if (str == NULL) {
        printf_puts("(null)", written);
        return;
    }

    while (*str)
        printf_char(*str++, written);
}

static void printf_utoa_base(register unsigned int x,
                             register unsigned int base, int *written)
{
    static const char digits[] = "0123456789abcdef";
    char buf[MAXBUF - 1];

    register char *c = &buf[MAXBUF - 1];

    do {
        *c-- = digits[x % base];
        x /= base;
    } while (x != 0);

    while (++c <= &buf[MAXBUF - 1])
        printf_char(*c, written);
}

static void printf_itoa(register int x, int *written)
{
    if (x < 0) {
        printf_char('-', written);
        printf_utoa_base(-x, 10, written);
    } else {
        printf_utoa_base(x, 10, written);
    }
}

static void printf_step(char c, int *index, int *written, va_list *parameters)
{
    switch (c) {
    case TOK_DECIMAL:
        printf_itoa(va_arg(*parameters, int), written);
        break;

    case TOK_UNSIGNED:
        printf_utoa_base(va_arg(*parameters, unsigned int), 10, written);
        break;

    case TOK_OCTAL:
        printf_char('0', written);
        printf_utoa_base(va_arg(*parameters, unsigned int), 8, written);
        break;

    case TOK_HEX:
        printf_puts("0x", written);
        printf_utoa_base(va_arg(*parameters, unsigned int), 16, written);
        break;

    case TOK_BINARY:
        printf_utoa_base(va_arg(*parameters, unsigned int), 2, written);
        printf_char('b', written);
        break;

    case TOK_POINTER:
        printf_puts("0x", written);
        printf_utoa_base((unsigned int)va_arg(*parameters, void *), 16,
                         written);
        break;

    case TOK_STR:
        printf_puts(va_arg(*parameters, char *), written);
        break;

    case TOK_ASCII:
        printf_char(va_arg(*parameters, int), written);
        break;

    case TOK_ESCAPE:
        printf_char(TOK_DELIMITER, written);
        break;

    default:
        printf_char(TOK_DELIMITER, written);
        *index -= 1;
        break;
    }

    *index += 1;
}

int printf(const char *format, ...)
{
    if (format == 0)
        return 0;

    va_list parameters;
    int written = 0;

    va_start(parameters, format);

    for (int i = 0; format[i]; ++i) {
        char c = format[i];

        if (c != TOK_DELIMITER) {
            printf_char(c, &written);
            continue;
        }

        printf_step(format[i + 1], &i, &written, &parameters);
    }

    va_end(parameters);

    return written;
}
