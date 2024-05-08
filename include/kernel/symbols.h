#ifndef KERNEL_SYMBOLS_H
#define KERNEL_SYMBOLS_H

#include <utils/compiler.h>
#include <utils/types.h>

/**
 * @brief A single symbol entry inside the symbol table
 * @info The structure is **ALWAYS** followed by the symbol's name's string.
 */
typedef struct PACKED {
    /// Size of the symbol (i.e. sizeof(size) + sizeof(address) + sizeof(name))
    u32 size;
    /// Address of the symbol
    u32 address;
} kernel_symbol_t;

/**
 * @brief The symbol table for the kernel
 *
 * This table contains the address and the name of every symbol defined inside
 * our kernel's code.
 *
 * @info To reduce the size of this table, we only keep function symbols. Static
 * variables and other variables are ignored. This may change in the future.
 */
typedef struct PACKED {
    u32 count;
    const kernel_symbol_t symbols[];
} kernel_symbol_table_t;

ALWAYS_INLINE const char *kernel_symbol_name(const kernel_symbol_t *sym)
{
    return ((const char *)sym + sizeof(kernel_symbol_t));
}

/** @brief Find the kernel symbol associated with a given address.
 *
 * The returned symbol is the one with the biggest address lower than the one
 * we're looking for.
 */
const kernel_symbol_t *symbol_from_address(u32 address);

#endif /* KERNEL_SYMBOLS_H */
