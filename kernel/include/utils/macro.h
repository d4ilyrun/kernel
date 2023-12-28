#ifndef UTILS_MACRO_H
#define UTILS_MACRO_H

/* Clear the nth bit */
#define BIT_MASK(_x, _n) ((_x) & ~(1 << (_n)))

/* Set the nth bit */
#define BIT_SET(_x, _n) ((_x) | (1 << (_n)))

/* Read the nth bit */
#define BIT(_x, _n) ((_x) & (1 << (_n)))

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

#endif /* UTILS_MACRO_H */
