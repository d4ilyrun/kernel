#ifndef LIBALGO_H
#define LIBALGO_H

/**
 * @file libalgo/libalgo.h
 *
 * @defgroup libalgo Libalgo
 *
 * Libalgo is my own implementation of different datastructures and algorithms
 * used by the kernel.
 *
 * They are not necessarily optimized, nor well design, but at least they exist.
 *
 */

/** Comparison function over two generic objects
 *
 *  @return 0 if both are equal, -1 if left is inferior, +1 if it is superior
 */
typedef int (*compare_t)(const void *left, const void *right);

#define COMPARE_EQ 0
#define COMPARE_LESS -1
#define COMPARE_GREATER 1

#endif /* LIBALGO_H */
