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

#include <stdint.h>

#include "compiler.h"

/** Generate the nth power of 2 (nth bit set) */
#define BIT(_n) (1 << (_n))

/** Clear the nth bit */
#define BIT_CLEAR(_x, _n) ((_x) & ~BIT((_n)))

/** Set the nth bit */
#define BIT_SET(_x, _n) ((_x) | BIT((_n)))

/** Read the nth bit */
#define BIT_READ(_x, _n) ((_x)&BIT((_n)))

/** @brief Find the index of the last set bit inside @c word */
static ALWAYS_INLINE unsigned long bit_first_one(unsigned long word)
{
    return __builtin_ctzl(word);
}

/** @brief Find the index of the first set bit inside @c word */
static ALWAYS_INLINE unsigned long bit_last_one(unsigned long word)
{
    return (8 * sizeof(word)) - __builtin_clzl(word) - 1;
}

/** @brief Find the index of the first unset bit inside @c word */
static ALWAYS_INLINE unsigned long bit_first_zero(unsigned long word)
{
    return __builtin_ctzl(~word);
}

/** @brief Find the index of the last unset bit inside @c word */
static ALWAYS_INLINE unsigned long bit_last_zero(unsigned long word)
{
    return (8 * sizeof(word)) - __builtin_clzl(~word) - 1;
}

/** @brief Compute the nex highest power of 2 for a 32bit integer
 *  @see https://graphics.stanford.edu/%7Eseander/bithacks.html#RoundUpPowerOf2
 */
static ALWAYS_INLINE uint32_t bit_next_pow32(uint32_t val)
{
    --val;

    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;

    return val + 1;
}

#endif /* UTILS_BITS_H */
