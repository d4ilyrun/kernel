/**
 * @brief Bitmap
 *
 * @file libalgo/bitmap.h
 * @author Léo DUBOIN <leo@duboin.com>
 *
 * @defgroup bitmap Bitmap
 * @ingroup libalgo
 *
 * @{
 *
 * # Bitmap
 *
 * A bitmap is a simple array of bytes, in which each and every bit corresponds
 * to an item and can be set to 'present' or not. This is useful to easily keep
 * track of the state of a large number of elements.
 *
 */

#ifndef LIBALGO_BITMAP_H
#define LIBALGO_BITMAP_H

#include <kernel/types.h>

#include <utils/bits.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Basic unit used by the bitmap
 *
 * A block is just a group of bits
 */
typedef native_t bitmap_block_t;

/** @brief A bitmap instance
 *  A bitmap is just a simple list of bytes (bitmap_block_t)
 */
typedef bitmap_block_t *bitmap_t;

#define BITMAP_BLOCK_SIZE (8 * sizeof(bitmap_block_t))

#define BITMAP_OFFSET(_index) ((_index) / BITMAP_BLOCK_SIZE)

/** @brief Declare a bitmap variable of a given size */
#define BITMAP(_name, _size) \
    bitmap_block_t _name[BITMAP_OFFSET(NON_ZERO(_size) - 1) + 1]

/**
 * Fill a bitmap with zeroes.
 */
#define bitmap_fill_zeroes(bitmap) memset(bitmap, 0, sizeof(bitmap));

/**
 * Fill a bitmap with ones.
 */
#define bitmap_fill_ones(bitmap) memset(bitmap, 0xff, sizeof(bitmap));

/**
 * @brief Read the value at a given index inside a bitmap
 *
 * @warning This function does not perform any bound checking
 *
 * @return Whether state of the nth element in a bitmap
 */
static ALWAYS_INLINE bool bitmap_read(const bitmap_t bitmap, uint32_t index)
{
    return BIT_READ(bitmap[BITMAP_OFFSET(index)], index % BITMAP_BLOCK_SIZE);
}

/**
 * @brief Set a value at a given index inside a bitmap as present
 *
 * @warning This function does not perform any bound checking
 */
static ALWAYS_INLINE void bitmap_set(bitmap_t bitmap, uint32_t index)
{
    BIT_SET(bitmap[BITMAP_OFFSET(index)], index % BITMAP_BLOCK_SIZE);
}

/**
 * @brief Set a value at a given index inside a bitmap as absent
 *
 * @warning This function does not perform any bound checking
 */
static ALWAYS_INLINE void bitmap_clear(bitmap_t bitmap, uint32_t index)
{
    BIT_CLEAR(bitmap[BITMAP_OFFSET(index)], index % BITMAP_BLOCK_SIZE);
}

/**
 * @brief Set a value at a given index inside a bitmap to the given value
 *
 * @warning This function does not perform any bound checking
 */
static ALWAYS_INLINE void
bitmap_assign(bitmap_t bitmap, uint32_t index, bool value)
{
    if (value)
        bitmap_set(bitmap, index);
    else
        bitmap_clear(bitmap, index);
}

static ALWAYS_INLINE int __bitmap_first(bitmap_t bitmap, uint32_t size)
{
    uint32_t blocks = BITMAP_OFFSET(NON_ZERO(size) - 1) + 1;

    for (uint32_t i = 0; i < blocks; ++i) {
        bitmap_block_t block = bitmap[i];

        if (block)
            return i * BITMAP_BLOCK_SIZE + __builtin_ctzl(block);
    }

    return -1;
}

static ALWAYS_INLINE int
__bitmap_first_clear(const bitmap_t bitmap, uint32_t size)
{
    uint32_t blocks = BITMAP_OFFSET(NON_ZERO(size) - 1) + 1;

    for (uint32_t i = 0; i < blocks; ++i) {
        bitmap_block_t block = ~bitmap[i];

        /* Mask off bits beyond the bitmap size in the last block */
        if (i == blocks - 1) {
            uint32_t rem = size % BITMAP_BLOCK_SIZE;

            if (rem)
                block &= (((bitmap_block_t)1 << rem) - 1);
        }

        if (block)
            return i * BITMAP_BLOCK_SIZE + __builtin_ctzl(block);
    }

    return -1;
}

static ALWAYS_INLINE int __bitmap_last(bitmap_t bitmap, uint32_t size)
{
    uint32_t blocks = BITMAP_OFFSET(size - 1) + 1;

    if (!blocks)
        return -1;

    uint32_t rem = size % BITMAP_BLOCK_SIZE;

    for (uint32_t i = blocks; i-- > 0;) {
        bitmap_block_t block = bitmap[i];

        if (i == blocks - 1 && rem)
            block &= (((bitmap_block_t)1 << rem) - 1);

        if (block)
            return i * BITMAP_BLOCK_SIZE +
                   (BITMAP_BLOCK_SIZE - 1 - __builtin_clzl(block));
    }

    return -1;
}

static ALWAYS_INLINE int
__bitmap_last_clear(const bitmap_t bitmap, uint32_t size)
{
    uint32_t blocks = BITMAP_OFFSET(NON_ZERO(size) - 1) + 1;

    for (uint32_t i = blocks; i-- > 0;) {
        bitmap_block_t block = ~bitmap[i];

        /* Mask off unused bits in last block */
        if (i == blocks - 1) {
            uint32_t rem = size % BITMAP_BLOCK_SIZE;

            if (rem)
                block &= (((bitmap_block_t)1 << rem) - 1);
        }

        if (block) {
            /* position of highest set bit in inverted block */
            return i * BITMAP_BLOCK_SIZE +
                   (BITMAP_BLOCK_SIZE - 1 - __builtin_clzl(block));
        }
    }

    return -1;
}

/**
 * Find the first bit set inside a bitmap.
 *
 * @return The bit's index, or -1 if all bits are clear.
 */
#define bitmap_last(bitmap) __bitmap_last(bitmap, sizeof(bitmap));

/**
 * Find the last bit set inside a bitmap.
 *
 * @return The bit's index, or -1 if all bits are clear.
 */
#define bitmap_first(bitmap) __bitmap_first(bitmap, sizeof(bitmap));

/**
 * Find the first bit set inside a bitmap.
 *
 * @return The bit's index, or -1 if all bits are clear.
 */
#define bitmap_last_clear(bitmap) __bitmap_last_clear(bitmap, sizeof(bitmap));

/**
 * Find the last bit set inside a bitmap.
 *
 * @return The bit's index, or -1 if all bits are clear.
 */
#define bitmap_first_clear(bitmap) __bitmap_first_clear(bitmap, sizeof(bitmap));

/** @} */

#endif /* LIBALGO_BITMAP_H */
