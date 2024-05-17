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

#include <utils/bits.h>
#include <utils/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Basic unit used by the bitmap
 *
 * A block is just a group of bits
 *
 * @todo define arch default type (e.g. u64 for 64 bits)
 */
typedef uint32_t bitmap_block_t;

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
    bitmap[BITMAP_OFFSET(index)] =
        BIT_SET(bitmap[BITMAP_OFFSET(index)], index % BITMAP_BLOCK_SIZE);
}

/**
 * @brief Set a value at a given index inside a bitmap as absent
 *
 * @warning This function does not perform any bound checking
 */
static ALWAYS_INLINE void bitmap_clear(bitmap_t bitmap, uint32_t index)
{
    bitmap[BITMAP_OFFSET(index)] =
        BIT_CLEAR(bitmap[BITMAP_OFFSET(index)], index % BITMAP_BLOCK_SIZE);
}

/**
 * @brief Set a value at a given index inside a bitmap to the given value
 *
 * @warning This function does not perform any bound checking
 */
static ALWAYS_INLINE void bitmap_assign(bitmap_t bitmap, uint32_t index,
                                        bool value)
{
    if (value)
        bitmap_set(bitmap, index);
    else
        bitmap_clear(bitmap, index);
}

/** @} */

#endif /* LIBALGO_BITMAP_H */
