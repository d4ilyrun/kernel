/**
 * @file utlis/bits.h
 *
 * @defgroup utils_bits Bit manipulation
 * @ingroup utils
 *
 * This file contains common (or not) bitwise operations.
 *
 * @{
 */

#ifndef UTILS_BITS_H
#define UTILS_BITS_H

#include "compiler.h"

/** Generate the nth power of 2 (nth bit set) */
#define BIT(_n) (1 << (_n))

/** Clear the nth bit */
#define BIT_CLEAR(_x, _n) ((_x) & ~BIT((_n)))

/** Set the nth bit */
#define BIT_SET(_x, _n) ((_x) | BIT((_n)))

/** Read the nth bit */
#define BIT_READ(_x, _n) ((_x)&BIT((_n)))

/** @brief Find the index of the last set bit inside @word
 */
MAYBE_UNUSED static ALWAYS_INLINE unsigned long
bit_first_one(unsigned long word)
{
    return __builtin_ctzl(word);
}

/** @brief Find the index of the first set bit inside @word
 */
MAYBE_UNUSED static ALWAYS_INLINE unsigned long bit_last_one(unsigned long word)
{
    return (8 * sizeof(word)) - __builtin_clzl(word) - 1;
}

/** @brief Find the index of the first unset bit inside @word
 */
MAYBE_UNUSED static ALWAYS_INLINE unsigned long
bit_first_zero(unsigned long word)
{
    return __builtin_ctzl(~word);
}

/** @brief Find the index of the last unset bit inside @word
 */
MAYBE_UNUSED static ALWAYS_INLINE unsigned long
bit_last_zero(unsigned long word)
{
    return (8 * sizeof(word)) - __builtin_clzl(~word) - 1;
}

#endif /* UTILS_BITS_H */
