#include <kernel/logger.h>
#include <kernel/symbols.h>

#include <utils/compiler.h>
#include <utils/macro.h>

SECTION(".kernel_symbols")
static volatile const kernel_symbol_table_t kernel_symbols;

static ALWAYS_INLINE const kernel_symbol_t *
kernel_symbol_next(const kernel_symbol_t *symbol)
{
    return (const kernel_symbol_t *)((u8 *)symbol + symbol->size);
}

const kernel_symbol_t *kernel_symbol_from_address(u32 address)
{
    const kernel_symbol_t *symbol =
        (const kernel_symbol_t *)kernel_symbols.symbols;

    for (u32 i = 0; i < kernel_symbols.count - 1; ++i) {
        const kernel_symbol_t *next = kernel_symbol_next(symbol);
        if (next->address > address)
            break;
        symbol = next;
    }

    return symbol;
}
