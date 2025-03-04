/**
 * @file kernel/symbols.h
 *
 * @defgroup symbols Kernel Symbols
 * @ingroup kernel
 *
 * # Kernel Symbols
 *
 * For debugging purposes, we want to be able to retrieve the name of a kernel's
 * function given its address. This can let us identify a faulting address more
 * easily.
 *
 * To do so, during the compilation process, we manually inject the list of
 * kernel's functions names, as well as their respective address, inside of the
 * @c .kernel_symbols ELF segment.
 *
 * @{
 */

#ifndef KERNEL_SYMBOLS_H
#define KERNEL_SYMBOLS_H

#include <kernel/types.h>

#include <utils/compiler.h>

/**
 * @struct kernel_symbol
 * @brief A single symbol entry inside the symbol table
 * @info The structure is **ALWAYS** followed by the symbol's name's string.
 */
typedef struct PACKED kernel_symbol {
    /** Size of the symbol (sizeof(size) + sizeof(address) + strlen(name)) */
    u32 size;
    /** Address of the symbol */
    u32 address;
} kernel_symbol_t;

/**
 * @brief The symbol table for the kernel
 * @struct kernel_symbol_table
 *
 * This table contains the address and the name of every symbol defined inside
 * our kernel's code.
 *
 * @note To reduce the size of this table, we only keep function symbols. Static
 * variables and other variables are ignored. This may change in the future.
 */
typedef struct PACKED kernel_symbol_table {
    u32 count;
    const kernel_symbol_t symbols[];
} kernel_symbol_table_t;

/** Retrieve a kernel symbol's raw name */
ALWAYS_INLINE const char *kernel_symbol_name(const kernel_symbol_t *sym)
{
    return ((const char *)sym + sizeof(kernel_symbol_t));
}

/** @brief Find the kernel symbol associated with a given address.
 *
 * The returned symbol is the one with the biggest address lower than the one
 * we're looking for.
 */
const kernel_symbol_t *kernel_symbol_from_address(u32 address);

#endif /* KERNEL_SYMBOLS_H */
