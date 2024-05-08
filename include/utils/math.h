/**
 * @file utils/math.h
 *
 * @author Léo DUBOIN <leo@duboin.com>
 *
 * Collection of macros to perform simple and common math operations.
 */

#ifndef UTILS_MATH_H
#define UTILS_MATH_H

#include "types.h"

/** @brief Compute the maximum value between 2 numbers */
#define MAX(_x, _y)                    \
    ({                                 \
        __typeof__(_x) _max1 = (_x);   \
        __typeof__(_y) _max2 = (_y);   \
        _max1 > _max2 ? _max1 : _max2; \
    })

/** @brief Compute the minimum value between 2 numbers */
#define MIN(_x, _y)                    \
    ({                                 \
        __typeof__(_x) _max1 = (_x);   \
        __typeof__(_y) _max2 = (_y);   \
        _max1 < _max2 ? _max1 : _max2; \
    })

/** @brief Compute the absolute value of a number */
#define ABS(_x)                     \
    ({                              \
        __typeof__(_x) _tmp = (_x); \
        _tmp < 0 ? -_tmp : _tmp;    \
    })

#define __align_mask(_value, _power) ((__typeof__(_value))((_power)-1))

/** @brief Align @_value to the next multiple of @_power
 *  @warning This macro assumes _power is a power of 2. When using an arbitrary
 *           value, you must use @link round_up instead.
 */
#define align_up(_value, _power) \
    ((((_value)-1) | __align_mask(_value, _power)) + 1)

/** @brief Align @_value to the previous multiple of @_power
 *  @warning This macro assumes _power is a power of 2. When using an arbitrary
 *           value, you must use @link round_down instead.
 */
#define align_down(_value, _power) ((_value) & ~__align_mask(_value, _power))

/** @brief Round @value to the next multiple of @alignment
 *
 *  When rounding to a power of two, prefere using @link align_up instead.
 *
 *  @warning This function does not check against overflow
 */
static inline u32 round_up(u32 value, u32 alignment)
{
    if (alignment == 0)
        return value;

    u32 offset = value % alignment;
    if (offset)
        value += alignment - offset; // WARNING: Offset can occur here!

    return value;
}

/** @brief Round @value to the previous multiple of @alignment
 *
 *  When rounding to a power of two, prefere using @link align_down instead.
 */
static inline u32 round_down(u32 value, u32 alignment)
{
    if (alignment == 0)
        return value;

    return value - (value % alignment);
}

#endif /* UTILS_MATH_H */
