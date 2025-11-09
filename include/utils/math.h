/**
 * @file utils/math.h
 * @author Léo DUBOIN <leo@duboin.com>
 *
 * @defgroup utils_math Mathematical operations
 * @ingroup utils
 *
 * Collection of macros to perform simple and common math operations.
 *
 * @{
 */

#ifndef UTILS_MATH_H
#define UTILS_MATH_H

#include <kernel/types.h>

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

/** @return whether a value is a power of two */
#define is_power_of_2(_x) (_x != 0 && ((_x & (_x - 1)) == 0))

#define __align_mask(_value, _power) ((__typeof__(_value))((_power)-1))

/** @brief Align @c _value to the next multiple of @c _power
 *  @warning This macro assumes _power is a power of 2. When using an arbitrary
 *           value, you must use @ref round_up instead.
 */
#define align_up(_value, _power) \
    ((((_value)-1) | __align_mask(_value, _power)) + 1)

/** @brief Align @c _value to the previous multiple of @c _power
 *  @warning This macro assumes _power is a power of 2. When using an arbitrary
 *           value, you must use @ref round_down instead.
 */
#define align_down(_value, _power) ((_value) & ~__align_mask(_value, _power))

/** @return Whether a value is aligned onto a given boundary */
#define is_aligned(_value, _alignment) (!((_value) % (_alignment)))

#define align_down_ptr(_ptr, _power) ((void *)align_down((vaddr_t)_ptr, _power))
#define align_up_ptr(_ptr, _power) ((void *)align_up((vaddr_t)_ptr, _power))
#define is_aligned_ptr(_ptr, _alignment) is_aligned((vaddr_t)_ptr, _alignment)

/** @brief Round @c value to the next multiple of @c alignment
 *
 *  When rounding to a power of two, prefere using @ref align_up instead.
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

/** @brief Round @c value to the previous multiple of @c alignment
 *
 *  When rounding to a power of two, prefere using @ref align_down instead.
 */
static inline u32 round_down(u32 value, u32 alignment)
{
    if (alignment == 0)
        return value;

    return value - (value % alignment);
}

#endif /* UTILS_MATH_H */
