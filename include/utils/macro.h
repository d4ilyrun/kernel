#ifndef UTILS_MACRO_H
#define UTILS_MACRO_H

/**
 * @defgroup utils
 * @{
 */

/** Retrieve the most significant bytes from 16bit integer */
#define MSB(_x) ((_x) >> 8)
/** Retrieve the less significant bytes from 16bit integer */
#define LSB(_x) ((_x)&0xFF)

/** Loop infinitely while the condition @c _cond is not met */
#define WAIT_FOR(_cond)  \
    do {                 \
        while (!(_cond)) \
            /* wait */;  \
    } while (0)

/** Check if x is strictly between l and h (l < x < h) */
#define BETWEEN(_x, _l, _h) ((_l) < (_x) && (_x) < (_h))

/** Check if x is between l and h (l =< x =< h) */
#define IN_RANGE(_x, _l, _h) ((_l) <= (_x) && (_x) <= (_h))

/** Avoid compiler warning when not using a symbol */
#define UNUSED(_x) (void)(_x);

/** Compute the number of element inside an array at compile time */
#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))

/** Retun the result of a comparison, similar to what's returned by strcmp */
#define RETURN_CMP(_x, _y)                 \
    do {                                   \
        typeof((_x)) _tmp_x = (_x);        \
        typeof((_y)) _tmp_y = (_y);        \
        if (_tmp_x == _tmp_y)              \
            return 0;                      \
        return (_tmp_x < _tmp_y) ? -1 : 1; \
    } while (0);

#endif /* UTILS_MACRO_H */
