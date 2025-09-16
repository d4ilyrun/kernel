#include <kernel/console.h>
#include <kernel/devices/uart.h>
#include <kernel/printk.h>
#include <kernel/symbols.h>
#include <kernel/types.h>

#include <utils/compiler.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

static_assert(sizeof(int) == sizeof(long), "Unsupported architecture: "
                                           "sizeof(long) != sizeof(int)");

#define BUFFER_SIZE 64

static char printk_buffer[BUFFER_SIZE];
static size_t printk_buffer_index = 0;

#define TOK_DELIMITER '%'

// supported tokens
#define TOK_STR 's'
#define TOK_ASCII 'c'
#define TOK_DECIMAL 'd'
#define TOK_HEX 'x'
#define TOK_HEX_CAPITALIZED 'X'
#define TOK_OCTAL 'o'
#define TOK_UNSIGNED 'u'
#define TOK_POINTER 'p'
#define TOK_BINARY 'b' // custom token
#define TOK_ESCAPE TOK_DELIMITER

/*
 * This printk implementation comes custom pointer suffixes to print more
 * complex types.
 *
 * Print the name of a kernel symbol:
 * * %ps: kernel_symbol
 * * %pS: kernel_symbol+0xoffset
 * * %pe: err_to_str(*(error_t *)ptr)
 * * %pE: err_to_str(ERR_FROM_PTR(ptr))
 */
#define TOK_POINTER_SYMBOL 's'
#define TOK_POINTER_SYMBOL_OFFSET 'S'
#define TOK_POINTER_ERROR 'e'
#define TOK_POINTER_ERROR_POINTER 'E'

#define TOK_LEN_ELL 'l'
#define TOK_LEN_SHORT 'h'
#define TOK_LEN_SIZE_T 'z'
#define TOK_LEN_PTRDIFF_T 't'

#define MAXBUF (sizeof(long) * 8) // 32B max (times 8 bits)

/// man 3 printf: Flag characters
typedef struct {
    bool invalid;
    bool alternate_form;
    enum {
        PADDING_ZERO = '0',
        PADDING_SPACE = ' '
    } pad_char;
    enum {
        PADDING_LEFT,
        PADDING_RIGHT
    } pad_side;
    enum {
        SIGN_MINUS_ONLY,
        SIGN_PLUS_SPACE = ' ',
        SIGN_BOTH = '+'
    } sign_character;
} flags_t;

/// man 3 printf: Precision
typedef struct {
    bool present; // This field is optional
    bool invalid;
    unsigned int precision;
    unsigned int argument;
} precision_t;

/// man 3 printf: Length modifier
typedef struct {
    bool invalid;
    unsigned char ell; // Number of 'ell' before the current token
    unsigned char h;   // Number of 'h' before the current token
    unsigned char z;   // Following conversion corresponds to a (s)size_t
    unsigned char t;   // Following conversion corresponds to a ptrdiff_t
} length_modifier_t;

typedef struct {
    bool invalid;
    flags_t flags;
    length_modifier_t length;
    unsigned int field_with; ///< man 3 printf: Field width
    precision_t precision;
    bool capitalize; ///< For %X conversions
} printk_ctx_t;

static inline bool __attribute__((always_inline)) isdigit(char c)
{
    return '0' <= (c) && (c) <= '9';
}

static void printk_buffer_flush(void)
{
    console_write(printk_buffer, printk_buffer_index);
    printk_buffer_index = 0;
}

static void printk_buffer_put(char c)
{
    if (printk_buffer_index == BUFFER_SIZE - 1) {
        printk_buffer_flush();
    }

    printk_buffer[printk_buffer_index++] = c;
}

/// PRINTERS
///
/// These functions are used to print the string and arguments according to the
/// given format.
///
/// They require the printing context to be known, and as such should be called
/// after we are done with the PARSERS functions.

static inline void __attribute__((always_inline))
printk_char(register char c, int *written)
{
    printk_buffer_put(c);

    if (c == '\n' || c == '\r')
        printk_buffer_flush();

    *written += 1;
}

// NOLINTNEXTLINE(misc-no-recursion)
static void
printk_puts(register const char *str, printk_ctx_t *ctx, int *written)
{
    if (str == NULL) {
        printk_puts("(null)", NULL, written);
        return;
    }

    while (*str) {
        // precision: the maximum number of character to be printed
        if (ctx && ctx->precision.present && ctx->precision.precision-- == 0)
            break;
        printk_char(*str++, written);
    }
}

static void printk_utoa_base(register unsigned long long x,
                             register unsigned int base,
                             const printk_ctx_t *ctx, int *written)
{
    static const char *digits[2] = {
        "0123456789abcdef",
        "0123456789ABCDEF",
    };

    char buf[MAXBUF];

    register char *c = &buf[MAXBUF - 1];
    char *const end = c;

    do {
        *c-- = digits[ctx->capitalize][x % base];
        x /= base;
    } while (x != 0);

    unsigned int length = end - c;
    unsigned int total_length = length;

    if (ctx->precision.present) {
        total_length = (length > ctx->precision.precision)
                         ? length
                         : ctx->precision.precision;
    }

    if (ctx->flags.alternate_form) {
        switch (base) {
        case 2:
            printk_char('b', written);
            break;
        case 8:
            // For o conversions, the first character of the output string
            // is made 0 (by prefixing a 0 if it was not 0 already).
            if (c[1] != 0)
                break;
            if (ctx->flags.pad_side == PADDING_LEFT &&
                ctx->flags.pad_char == PADDING_ZERO &&
                total_length < ctx->field_with)
                break;
            printk_char('0', written);
            break;
        case 16:
            if (ctx->capitalize)
                printk_puts("0X", NULL, written);
            else
                printk_puts("0x", NULL, written);
            break;
        default:
            break;
        }
    }

    if (ctx->flags.pad_side == PADDING_RIGHT) {
        // prepend enough '0' to match minimum precision
        while (length++ < total_length)
            printk_char('0', written);
        while (++c != &buf[MAXBUF])
            printk_char(*c, written);
    }

    if (total_length < ctx->field_with) {
        for (unsigned int i = total_length; i < ctx->field_with; ++i)
            printk_char(ctx->flags.pad_char, written);
    }

    if (ctx->flags.pad_side == PADDING_LEFT) {
        // prepend enough '0' to match minimum precision
        while (length++ < total_length)
            printk_char('0', written);
        while (++c != &buf[MAXBUF])
            printk_char(*c, written);
    }
}

static void printk_itoa(register int x, const printk_ctx_t *ctx, int *written)
{
    if (x < 0) {
        printk_char('-', written);
        printk_utoa_base(-x, 10, ctx, written);
    } else {
        if (ctx->flags.sign_character != SIGN_MINUS_ONLY)
            printk_char(ctx->flags.sign_character, written);
        printk_utoa_base(x, 10, ctx, written);
    }
}

static void printk_kernel_symbol(vaddr_t addr, bool print_offset,
                                 printk_ctx_t *ctx, int *written)
{
    const kernel_symbol_t *symbol = kernel_symbol_from_address(addr);
    off_t offset = addr - symbol->address;

    ctx->precision.present = false;
    printk_puts(kernel_symbol_name(symbol), ctx, written);

    if (print_offset) {
        ctx->flags.alternate_form = true;
        printk_char('+', written);
        printk_utoa_base(offset, 16, ctx, written);
    }
}

static void
printk_err_to_str(void *ptr, bool raw_error, printk_ctx_t *ctx, int *written)
{
    error_t err;

    err = (raw_error) ? *(error_t *)ptr : ERR_FROM_PTR(ptr);

    ctx->precision.present = false;
    printk_puts(err_to_str(err), ctx, written);
}

/// PARSERS
///
/// These functions are used to parse the printing context from the
/// given format (padding, flags, size of fields, ...).

static flags_t printk_flags_characters(const char *format, int *index)
{
    flags_t flags = {
        .pad_char = PADDING_SPACE,
        .pad_side = PADDING_LEFT,
        .sign_character = SIGN_MINUS_ONLY,
    };

    bool parsing = true;

    while (parsing) {
        switch (format[*index]) {
        case '0':
            flags.pad_char = PADDING_ZERO;
            break;
        case '-':
            flags.pad_side = PADDING_RIGHT;
            break;
        case '#':
            flags.alternate_form = true;
            break;
        case ' ':
            flags.sign_character = SIGN_PLUS_SPACE;
            break;
        case '+':
            flags.sign_character = SIGN_BOTH;
            break;
        default:
            parsing = false;
            break;
        }

        if (parsing)
            *index += 1;
    }

    // If the 0 and - flags both appear, the 0 flag is ignored.
    if (flags.pad_side == PADDING_RIGHT)
        flags.pad_char = PADDING_SPACE;

    return flags;
}

static unsigned int printk_field_width(const char *format, int *index)
{
    unsigned int width = 0;

    while (isdigit(format[*index])) {
        width = 10 * width + (format[*index] - '0');
        *index += 1;
    }

    return width;
}

static precision_t printk_precision(const char *format, int *index)
{
    precision_t precision = {
        .present = false,
        .invalid = false,
        .precision = 0,
    };

    if (format[*index] != '.')
        return precision;

    precision.present = true;
    *index += 1;

    // A negative precision is taken as if the precision were omitted
    bool ignore = false;

    switch (format[*index]) {

    case '*':
        // the actual logic is not implemented, but we need to parse it anyway
        precision.present = false;

        *index += 1;
        if (isdigit(format[*index])) {
            precision.argument = printk_field_width(format, index);
            if (format[*(index++)] != '$')
                precision.invalid = true;
        }
        break;

    case '-':
        ignore = true;
        *index += 1;
        // cannot have '%.-d' for example
        if (!isdigit(format[*index])) {
            precision.invalid = true;
            return precision;
        }

        __attribute__((fallthrough));

    default:
        precision.precision = printk_field_width(format, index);
        break;
    }

    if (ignore)
        precision.precision = 0;

    return precision;
}

static length_modifier_t printk_length_modifiers(const char *format, int *index)
{
    length_modifier_t length = {0};

    switch (format[*index]) {
    case TOK_LEN_ELL:
        while (format[(*index)++] == TOK_LEN_ELL)
            length.ell += 1;
        *index -= 1; // skipped over the next character
        return length;

    case TOK_LEN_SHORT:
        while (format[(*index)++] == TOK_LEN_SHORT)
            length.h += 1;
        *index -= 1; // skipped over the next character
        return length;

    case TOK_LEN_SIZE_T:

        length.z = 1;
        break;

    case TOK_LEN_PTRDIFF_T:
        length.t = 1;
        break;

    case 'q':
        length.ell = 2;
        break;

    case '\0':
        length.invalid = true;
        return length;

    default:
        return length;
    }

    *index += 1;

    return length;
}

static void printk_unsigned(register int base, va_list *parameters,
                            const printk_ctx_t *ctx, int *written)
{
    // Here we know that sizeof(int) == sizeof(long) anyway and skip the
    // case were we only have a single 'l' modifier (cf. static_assert)
    if (ctx->length.ell >= 2)
        printk_utoa_base(va_arg(*parameters, unsigned long long), base, ctx,
                         written);
    else if (ctx->length.h == 1)
        printk_utoa_base((unsigned short)va_arg(*parameters, unsigned int),
                         base, ctx, written);
    else if (ctx->length.h >= 2)
        printk_utoa_base((unsigned char)va_arg(*parameters, unsigned int), base,
                         ctx, written);
    else if (ctx->length.z)
        printk_utoa_base(va_arg(*parameters, size_t), base, ctx, written);
    else if (ctx->length.t)
        printk_utoa_base(va_arg(*parameters, ptrdiff_t), base, ctx, written);
    else
        printk_utoa_base(va_arg(*parameters, unsigned int), base, ctx, written);
}

static int printk_step_pointer(const char *c, int *written, va_list *parameters,
                               printk_ctx_t *ctx)
{
    int step_size = 1;

    switch (*c) {
    case TOK_POINTER_SYMBOL:
    case TOK_POINTER_SYMBOL_OFFSET:
        printk_kernel_symbol(va_arg(*parameters, vaddr_t),
                             *c == TOK_POINTER_SYMBOL_OFFSET, ctx, written);
        break;
    case TOK_POINTER_ERROR_POINTER:
    case TOK_POINTER_ERROR:
        printk_err_to_str(va_arg(*parameters, void *), *c == TOK_POINTER_ERROR,
                          ctx, written);
        break;
    default:
        printk_utoa_base((unsigned int)va_arg(*parameters, void *), 16, ctx,
                         written);
        step_size = 0;
    }

    return step_size;
}

static int
printk_step(const char *c, int *written, va_list *parameters, printk_ctx_t *ctx)
{
    int step_size = 1;

    switch (*c) {

    // Here we assume that sizeof(int) == sizeof(long) anyway and skip
    // the case were we only have a single 'l' modifier (cf.
    // static_assert)
    case TOK_DECIMAL:
        if (ctx->length.ell < 2)
            printk_itoa(va_arg(*parameters, int), ctx, written);
        else if (ctx->length.z)
            printk_itoa(va_arg(*parameters, ssize_t), ctx, written);
        else if (ctx->length.t)
            printk_itoa(va_arg(*parameters, ptrdiff_t), ctx, written);
        else
            printk_itoa(va_arg(*parameters, long long), ctx, written);
        break;

    case TOK_UNSIGNED:
        printk_unsigned(10, parameters, ctx, written);
        break;

    case TOK_OCTAL:
        printk_unsigned(8, parameters, ctx, written);
        break;

    case TOK_HEX_CAPITALIZED:
        ctx->capitalize = true;
        __attribute__((fallthrough));

    case TOK_HEX:
        printk_unsigned(16, parameters, ctx, written);
        break;

    case TOK_BINARY:
        printk_unsigned(2, parameters, ctx, written);
        break;

    case TOK_POINTER:
        step_size += printk_step_pointer(&c[1], written, parameters, ctx);
        break;

    case TOK_STR:
        printk_puts(va_arg(*parameters, char *), ctx, written);
        break;

    case TOK_ASCII:
        printk_char(va_arg(*parameters, int), written);
        break;

    case TOK_ESCAPE:
        printk_char(TOK_DELIMITER, written);
        break;

    default:
        return -1;
    }

    return step_size;
}

/// MAIN FUNCTIONS
///
/// These are the main functions, called by the user.
///
/// They use both the PARSERS and the PRINTERS to print the final
/// result.

int vprintk(const char *format, va_list parameters)
{
    int written = 0;
    int error = 0;
    int i = 0;

    while (format[i] != '\0') {

        char c = format[i++];

        // Also print in case we reached the end of the format string
        if (c != TOK_DELIMITER) {
            printk_char(c, &written);
            continue;
        }

        // After the delimiter, the argument is of the following format:
        //
        // %[flag_characters][field_with][.precision][length_modifier]conversion_specifier
        //
        // Refer to man 3 printf and the corresponding parsers for more
        // detailed explanations.

        const flags_t flags = printk_flags_characters(format, &i);
        const unsigned int width = printk_field_width(format, &i);
        const precision_t precision = printk_precision(format, &i);
        const length_modifier_t length = printk_length_modifiers(format, &i);

        printk_ctx_t ctx = (printk_ctx_t){
            .invalid = flags.invalid || length.invalid || precision.invalid,
            .flags = flags,
            .length = length,
            .field_with = width,
            .precision = precision,
        };

        if (ctx.invalid) {
            error = 1;
            continue;
        }

        int step_size = printk_step(&format[i], &written, &parameters, &ctx);
        if (step_size < 0) {
            printk_char(format[i], &written);
            step_size = 1;
            error = 1;
            continue;
        }

        i += step_size;
    }

    return error ? -1 : written;
}

int printk(const char *format, ...)
{
    va_list parameters;
    va_start(parameters, format);
    int res = vprintk(format, parameters);
    va_end(parameters);
    return res;
}
