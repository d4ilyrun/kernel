#ifndef UTILS_MACRO_H
#define UTILS_MACRO_H

/* Generate the nth power of 2 (nth bit set) */
#define BIT(_n) (1 << (_n))

/* Clear the nth bit */
#define BIT_MASK(_x, _n) ((_x) & ~BIT((_n)))

/* Set the nth bit */
#define BIT_SET(_x, _n) ((_x) | BIT((_n)))

/* Read the nth bit */
#define BIT_READ(_x, _n) ((_x)&BIT((_n)))

/* Most/Less significant byte from 16bit integer */
#define MSB(_x) ((_x) >> 8)
#define LSB(_x) ((_x)&0xFF)

#define WAIT_FOR(_cond)  \
    do {                 \
        while (!(_cond)) \
            /* wait */;  \
    } while (0)

/* Check if x is strictly between l and h (l < x < h). */
#define BETWEEN(_x, _l, _h) ((_l) < (_x) && (_x) < (_h))

/* Check if x is between l and h (l =< x =< h). */
#define IN_RANGE(_x, _l, _h) ((_l) <= (_x) && (_x) <= (_h))

/* Avoid compiler warning when not using a symbol */
#define UNUSED(_x) (void)(_x);

#define MAX(_x, _y)                    \
    ({                                 \
        __typeof__(_x) _max1 = (_x);   \
        __typeof__(_y) _max2 = (_y);   \
        _max1 > _max2 ? _max1 : _max2; \
    })

#define MIN(_x, _y)                    \
    ({                                 \
        __typeof__(_x) _max1 = (_x);   \
        __typeof__(_y) _max2 = (_y);   \
        _max1 < _max2 ? _max1 : _max2; \
    })

#define ABS(_x)                     \
    ({                              \
        __typeof__(_x) _tmp = (_x); \
        _tmp < 0 ? -_tmp : _tmp;    \
    })

#endif /* UTILS_MACRO_H */
