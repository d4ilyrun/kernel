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
#define BIT64(_n) (1ULL << (_n))

/** Clear the nth bit */
#define BIT_CLEAR(_var, _n) (_var &= ~BIT((_n)))

/** Set the nth bit */
#define BIT_SET(_var, _n) (_var |= BIT((_n)))

/** Read the nth bit */
#define BIT_READ(_x, _n) ((_x) & BIT((_n)))

static inline uint64_t BIT_ENABLE(uint64_t bit, unsigned int off, int enable)
{
    if (enable)
        return BIT_SET(bit, off);
    return BIT_CLEAR(bit, off);
}

// clang-format off

static inline uint16_t __bswap_16(uint16_t x)
{
    return  (x << 8) | (x >> 8);
}

static inline uint32_t __bswap_32(uint32_t x)
{
    return  ((x) << 24) |
            ((x & 0x0000FF00) << 8) |
            ((x & 0x00FF0000) >> 8) |
            ((x) >> 24);
}

static inline uint64_t __bswap_64(uint64_t x)
{
    return  ((x) >> 56) |
            ((x & 0x00FF000000000000ULL) >> 40) |
            ((x & 0x0000FF0000000000ULL) >> 24) |
            ((x & 0x000000FF00000000ULL) >> 8) |
            ((x & 0x00000000FF000000ULL) << 8) |
            ((x & 0x0000000000FF0000ULL) << 24) |
            ((x & 0x000000000000FF00ULL) << 40) |
            ((x) << 56);
}

// clang-format on

#ifdef ARCH_LITTLE_ENDIAN

#define htobe16(x) __bswap_16(x)
#define htole16(x) (uint16_t)(x)
#define be16toh(x) __bswap_16(x)
#define le16toh(x) (uint16_t)(x)

#define htobe32(x) __bswap_32(x)
#define htole32(x) (uint32_t)(x)
#define be32toh(x) __bswap_32(x)
#define le32toh(x) (uint32_t)(x)

#define htobe64(x) __bswap_64(x)
#define htole64(x) (uint64_t)(x)
#define be64toh(x) __bswap_64(x)
#define le64toh(x) (uint64_t)(x)

#else

#define htobe16(x) (uint16_t)(x)
#define htole16(x) __bswap_16(x)
#define be16toh(x) (uint16_t)(x)
#define le16toh(x) __bswap_16(x)

#define htobe32(x) (uint32_t)(x)
#define htole32(x) __bswap_32(x)
#define be32toh(x) (uint32_t)(x)
#define le32toh(x) __bswap_32(x)

#define htobe64(x) (uint64_t)(x)
#define htole64(x) __bswap_64(x)
#define be64toh(x) (uint64_t)(x)
#define le64toh(x) __bswap_64(x)

#endif

/** @brief Find the index of the first set bit inside @c word */
static ALWAYS_INLINE unsigned long bit_first_one(unsigned long word)
{
    return __builtin_ctzl(word);
}

/** @brief Find the index of the last set bit inside @c word */
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
