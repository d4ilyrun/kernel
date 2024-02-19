#include <kernel/devices/uart.h>

#include <utils/compiler.h>
#include <utils/types.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static_assert(sizeof(int) == sizeof(long),
              "Unsupported architecture: sizeof(long) != sizeof(int)");

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

#define TOK_LEN_ELL 'l'
#define TOK_LEN_SHORT 'h'
#define TOK_LEN_SIZE_T 'z'
#define TOK_LEN_PTRDIFF_T 't'

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

static void printf_utoa_base(register unsigned long long x,
                             register unsigned int base, int *written)
{
    static const char digits[] = "0123456789abcdef";
    char buf[MAXBUF];

    register char *c = &buf[MAXBUF - 1];

    do {
        *c-- = digits[x % base];
        x /= base;
    } while (x != 0);

    while (++c != &buf[MAXBUF])
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

typedef struct printf_ctx {
    char ell;     // Number of 'ell' before the current token
    char h;       // Number of 'h' before the current token
    char z;       // Following conversion corresponds to a (s)size_t
    char t;       // Following conversion corresponds to a ptrdiff_t
    char invalid; // Ill formed contex
} printf_ctx;

static printf_ctx printf_length_modifiers(const char *format, int *index)
{
    printf_ctx ctx = {0};

    switch (format[*index]) {
    case TOK_LEN_ELL:
        while (format[(*index)++] == TOK_LEN_ELL)
            ctx.ell += 1;
        *index -= 1; // skipped over the next character
        return ctx;

    case TOK_LEN_SHORT:
        while (format[(*index)++] == TOK_LEN_SHORT)
            ctx.h += 1;
        *index -= 1; // skipped over the next character
        return ctx;

    case TOK_LEN_SIZE_T:

        ctx.z = 1;
        break;

    case TOK_LEN_PTRDIFF_T:
        ctx.t = 1;
        break;

    case 'q':
        ctx.ell = 2;
        break;

    case '\0':
        ctx.invalid = 1;
        return ctx;

    default:
        return ctx;
    }

    *index += 1;

    return ctx;
}

static void printf_unsigned(register int base, va_list *parameters,
                            const printf_ctx *ctx, int *written)
{
    // Here we know that sizeof(int) == sizeof(long) anyway and skip the
    // case were we only have a single 'l' modifier (cf. static_assert)
    if (ctx->ell >= 2)
        printf_utoa_base(va_arg(*parameters, unsigned long long), base,
                         written);
    else if (ctx->h == 1)
        printf_utoa_base((unsigned short)va_arg(*parameters, unsigned int),
                         base, written);
    else if (ctx->h >= 2)
        printf_utoa_base((unsigned char)va_arg(*parameters, unsigned int), base,
                         written);
    else if (ctx->z)
        printf_utoa_base(va_arg(*parameters, size_t), base, written);
    else if (ctx->t)
        printf_utoa_base(va_arg(*parameters, ptrdiff_t), base, written);
    else
        printf_utoa_base(va_arg(*parameters, unsigned int), base, written);
}

static int printf_step(char c, int *written, va_list *parameters,
                       const printf_ctx *ctx)
{
    switch (c) {

    // Here we assume that sizeof(int) == sizeof(long) anyway and skip the
    // case were we only have a single 'l' modifier (cf. static_assert)
    case TOK_DECIMAL:
        if (ctx->ell < 2)
            printf_itoa(va_arg(*parameters, int), written);
        else if (ctx->z)
            printf_itoa(va_arg(*parameters, ssize_t), written);
        else if (ctx->t)
            printf_itoa(va_arg(*parameters, ptrdiff_t), written);
        else
            printf_itoa(va_arg(*parameters, long long), written);
        break;

    case TOK_UNSIGNED:
        printf_unsigned(10, parameters, ctx, written);
        break;

    case TOK_OCTAL:
        printf_unsigned(8, parameters, ctx, written);
        break;

    case TOK_HEX:
        printf_unsigned(16, parameters, ctx, written);
        break;

    case TOK_BINARY:
        printf_unsigned(2, parameters, ctx, written);
        break;

    case TOK_POINTER:
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
        return -1;
    }

    return 0;
}

int vprintf(const char *format, va_list parameters)
{
    int written = 0;
    int error = 0;

    int i = 0;
    while (format[i] != '\0') {

        char c = format[i++];

        // Also print in case we reached the end of the format string
        if (c != TOK_DELIMITER) {
            printf_char(c, &written);
            continue;
        }

        const printf_ctx ctx = printf_length_modifiers(format, &i);
        if (ctx.invalid) {
            error = 1;
            continue;
        }

        if (printf_step(format[i], &written, &parameters, &ctx) == -1) {
            printf_char(format[i], &written);
            error = 1;
            continue;
        }

        i += 1;
    }

    return error ? -1 : written;
}

int printf(const char *format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    int res = vprintf(format, parameters);
    va_end(parameters);
    return res;
}
